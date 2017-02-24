/*
 * Copyright © 2016 Lukas Rosenthaler, Andrea Bianco, Benjamin Geer,
 * Ivan Subotic, Tobias Schweizer, André Kilchenmann, and André Fatton.
 * This file is part of Sipi.
 * Sipi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * Sipi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Additional permission under GNU AGPL version 3 section 7:
 * If you modify this Program, or any covered work, by linking or combining
 * it with Kakadu (or a modified version of that library) or Adobe ICC Color
 * Profiles (or a modified version of that library) or both, containing parts
 * covered by the terms of the Kakadu Software Licence or Adobe Software Licence,
 * or both, the licensors of this Program grant you additional permission
 * to convey the resulting work.
 * See the GNU Affero General Public License for more details.
 * You should have received a copy of the GNU Affero General Public
 * License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <functional>
#include <cctype>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>      // Needed for memset
#include <utility>

#include <sys/types.h>
#include <sys/select.h>
#include <signal.h>
#include <poll.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h> //inet_addr
#include <unistd.h>    //write
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <pwd.h>
#include <syslog.h>
//
// openssl includes
//#include "openssl/applink.c"


#include "Global.h"
#include "SockStream.h"
#include "Server.h"
#include "LuaServer.h"
#include "Parsing.h"
#include "makeunique.h"

static const char __file__[] = __FILE__;

static std::mutex threadlock; // mutex to protect map of active threads
static std::mutex idlelock; // mutex to protect vector of idle threads (keep alive condition)
static std::mutex debugio; // mutex to protect debugging messages from threads

static std::vector<pthread_t> idle_thread_ids;

// The signal caught by the sig_thread function, used only for debugging.
static int signal_result = 0;

namespace shttps {

    const char loggername[] = "Sipi"; // see Global.h !!

    typedef struct {
        int sock;
#ifdef SHTTPS_ENABLE_SSL
        SSL *cSSL;
#endif
        std::string peer_ip;
        int peer_port;
        int commpipe_read;
        Server *serv;
    } TData;
    //=========================================================================

    /*!
     * Starts a thread just to catch all signals sent to the server process.
     * If it receives SIGINT or SIGTERM, tells the server to stop.
     */
    static void *sig_thread(void *arg) {
        Server *serverptr = static_cast<Server *>(arg);
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGPIPE);
        sigaddset(&set, SIGINT);
        sigaddset(&set, SIGTERM);

        int sig;

        while (true) {
            if ((sigwait(&set, &sig)) != 0) {
                signal_result = -1;
                return nullptr;
            }

            signal_result = sig;

            // If we get SIGINT or SIGTERM, shut down the server.
            // Ignore any other signals. We must in particular ignore
            // SIGPIPE.
            if (sig == SIGINT || sig == SIGTERM) {
                serverptr->stop();
                return nullptr;
            }
        }
    }
   //=========================================================================


    static void default_handler(Connection &conn, LuaServer &lua, void *user_data, void *hd)
    {
        conn.status(Connection::NOT_FOUND);
        conn.header("Content-Type", "text/text");
        conn.setBuffer();
        try {
            conn << "No handler available" << Connection::flush_data;
        }
        catch (InputFailure iofail) {
            return;
        }
        syslog(LOG_WARNING, "No handler available! Host: %s Uri: %s", conn.host().c_str(), conn.uri().c_str());

        return;
    }
    //=========================================================================


    void ScriptHandler(shttps::Connection &conn, LuaServer &lua, void *user_data, void *hd)
    {
        std::vector<std::string> headers = conn.header();
        std::string uri = conn.uri();

        std::string script = *((std::string *) hd);

        if (access(script.c_str(), R_OK) != 0) { // test, if file exists
            conn.status(Connection::NOT_FOUND);
            conn.header("Content-Type", "text/text; charset=utf-8");
            conn << "File not found\n";
            conn.flush();
            syslog(LOG_ERR, "ScriptHandler: %s not readable!", script.c_str());
            return;
        }

        size_t extpos = script.find_last_of('.');
        std::string extension;
        if (extpos != std::string::npos) {
            extension = script.substr(extpos + 1);
        }

        try {
            if (extension == "lua") { // pure lua
                std::ifstream inf;
                inf.open(script); //open the input file

                std::stringstream sstr;
                sstr << inf.rdbuf(); //read the file
                std::string luacode = sstr.str();//str holds the content of the file

                try {
                    if (lua.executeChunk(luacode) < 0) {
                        conn.flush();
                        return;
                    }
                }
                catch (Error &err) {
                    try {
						conn.setBuffer();
                        conn.status(Connection::INTERNAL_SERVER_ERROR);
                        conn.header("Content-Type", "text/text; charset=utf-8");
                        conn << "Lua Error:\r\n==========\r\n" << err << "\r\n";
                        conn.flush();
                    }
                    catch(int i) {
                        return;
                    }
                    syslog(LOG_ERR, "ScriptHandler: error executing lua script: %s", err.to_string().c_str());
                    return;
                }
                conn.flush();
            }
            else if (extension == "elua") { // embedded lua <lua> .... </lua>
                conn.setBuffer();
                std::ifstream inf;
                inf.open(script);//open the input file

                std::stringstream sstr;
                sstr << inf.rdbuf();//read the file
                std::string eluacode = sstr.str(); // eluacode holds the content of the file

                size_t pos = 0;
                size_t end = 0; // end of last lua code (including </lua>)
                while ((pos = eluacode.find("<lua>", end)) != std::string::npos) {
                    std::string htmlcode = eluacode.substr(end, pos - end);
                    pos += 5;

                    if (!htmlcode.empty()) conn << htmlcode; // send html...

                    std::string luastr;
                    if ((end = eluacode.find("</lua>", pos)) != std::string::npos) { // we found end;
                        luastr = eluacode.substr(pos, end - pos);
                        end += 6;
                    }
                    else {
                        luastr = eluacode.substr(pos);
                    }

                    try {
                        if (lua.executeChunk(luastr) < 0) {
                            conn.flush();
                            return;
                        }
                    }
                    catch (Error &err) {
                        try {
                            conn.status(Connection::INTERNAL_SERVER_ERROR);
                            conn.header("Content-Type", "text/text; charset=utf-8");
                            conn << "Lua Error:\r\n==========\r\n" << err << "\r\n";
                            conn.flush();
                        }
                        catch (InputFailure iofail) {
                            return;
                        }
                        syslog(LOG_ERR, "ScriptHandler: error executing lua chunk: %s", err.to_string().c_str());
                        return;
                    }
                }
                std::string htmlcode = eluacode.substr(end);
                conn << htmlcode;
                conn.flush();
            }
            else {
                conn.status(Connection::INTERNAL_SERVER_ERROR);
                conn.header("Content-Type", "text/text; charset=utf-8");
                conn << "Script has no valid extension: '" << extension << "' !";
                conn.flush();
                syslog(LOG_ERR, "ScriptHandler: error executing script, unknown extension: %s", extension.c_str());
            }
        }
        catch (InputFailure iofail) {
            return; // we have an io error => just return, the thread will exit
        }
        catch (Error &err) {
            try {
                conn.status(Connection::INTERNAL_SERVER_ERROR);
                conn.header("Content-Type", "text/text; charset=utf-8");
                conn << err;
                conn.flush();
            }
            catch (InputFailure iofail) {
                return;
            }
            syslog(LOG_ERR, "FileHandler: internal error: %s", err.to_string().c_str());
            return;
        }
    }
    //=========================================================================


    void FileHandler(shttps::Connection &conn, LuaServer &lua, void *user_data, void *hd)
    {
        std::vector<std::string> headers = conn.header();
        std::string uri = conn.uri();

        std::string docroot;
        std::string route;
        if (hd == nullptr) {
            docroot = ".";
            route = "/";
        }
        else {
            std::pair<std::string, std::string> tmp = *((std::pair<std::string, std::string> *)hd);
            docroot = *((std::string *) hd);
            route = tmp.first;
            docroot = tmp.second;
        }

        lua.add_servertableentry("docroot", docroot);
        if (uri.find(route) == 0) {
            uri = uri.substr(route.length());
            if (uri[0] != '/') uri = "/" + uri;
        }


        std::string infile = docroot + uri;


        if (access(infile.c_str(), R_OK) != 0) { // test, if file exists
            conn.status(Connection::NOT_FOUND);
            conn.header("Content-Type", "text/text; charset=utf-8");
            conn << "File not found\n";
            conn.flush();
            syslog(LOG_ERR, "FileHandler: %s not readable", infile.c_str());
            return;
        }

        struct stat s;
        if (stat(infile.c_str(), &s) == 0) {
            if (!(s.st_mode & S_IFREG)) { // we have not a regular file, do nothing!
                return;
            }
        }
        else {
            return;
        }

        std::pair<std::string, std::string> mime = Parsing::getFileMimetype(infile);

        size_t extpos = uri.find_last_of('.');
        std::string extension;
        if (extpos != std::string::npos) {
            extension = uri.substr(extpos + 1);
        }
        try {
            if ((extension == "html") && (mime.first == "text/html")) {
                conn.header("Content-Type", "text/html; charset=utf-8");
                conn.sendFile(infile);
            }
            else if (extension == "js") {
                conn.header("Content-Type", "application/javascript; charset=utf-8");
                conn.sendFile(infile);
            }
            else if (extension == "css") {
                conn.header("Content-Type", "text/css; charset=utf-8");
                conn.sendFile(infile);
            }
            else if (extension == "lua") { // pure lua
                conn.setBuffer();
                std::ifstream inf;
                inf.open(infile);//open the input file

                std::stringstream sstr;
                sstr << inf.rdbuf();//read the file
                std::string luacode = sstr.str();//str holds the content of the file

                try {
                    if (lua.executeChunk(luacode) < 0) {
                        conn.flush();
                        return;
                    }
                }
                catch (Error &err) {
                    try {
                        conn.status(Connection::INTERNAL_SERVER_ERROR);
                        conn.header("Content-Type", "text/text; charset=utf-8");
                        conn << "Lua Error:\r\n==========\r\n" << err << "\r\n";
                        conn.flush();
                    }
                    catch(int i) {
                        syslog(LOG_ERR, "FileHandler: error executing lua chunk!");
                        return;
                    }
                    syslog(LOG_ERR, "FileHandler: error executing lua chunk: %s", err.to_string().c_str());
                    return;
                }
                conn.flush();
            }
            else if (extension == "elua") { // embedded lua <lua> .... </lua>
                conn.setBuffer();
                std::ifstream inf;
                inf.open(infile);//open the input file

                std::stringstream sstr;
                sstr << inf.rdbuf();//read the file
                std::string eluacode = sstr.str(); // eluacode holds the content of the file

                size_t pos = 0;
                size_t end = 0; // end of last lua code (including </lua>)
                while ((pos = eluacode.find("<lua>", end)) != std::string::npos) {
                    std::string htmlcode = eluacode.substr(end, pos - end);
                    pos += 5;

                    if (!htmlcode.empty()) conn << htmlcode; // send html...

                    std::string luastr;
                    if ((end = eluacode.find("</lua>", pos)) != std::string::npos) { // we found end;
                        luastr = eluacode.substr(pos, end - pos);
                        end += 6;
                    }
                    else {
                        luastr = eluacode.substr(pos);
                    }

                    try {
                        if (lua.executeChunk(luastr) < 0) {
                            conn.flush();
                            return;
                        }
                    }
                    catch (Error &err) {
                        try {
                            conn.status(Connection::INTERNAL_SERVER_ERROR);
                            conn.header("Content-Type", "text/text; charset=utf-8");
                            conn << "Lua Error:\r\n==========\r\n" << err << "\r\n";
                            conn.flush();
                        }
                        catch (InputFailure iofail) { }
                        syslog(LOG_ERR, "FileHandler: error executing lua chunk: %s", err.to_string().c_str());
                        return;
                    }
                }
                std::string htmlcode = eluacode.substr(end);
                conn << htmlcode;
                conn.flush();
            }
            else {
                conn.header("Content-Type", mime.first + "; " + mime.second);
                conn.sendFile(infile);
            }
        }
        catch (InputFailure iofail) {
            return; // we have an io error => just return, the thread will exit
        }
        catch (Error &err) {
            try {
                conn.status(Connection::INTERNAL_SERVER_ERROR);
                conn.header("Content-Type", "text/text; charset=utf-8");
                conn << err;
                conn.flush();
            }
            catch (InputFailure iofail) { }
            syslog(LOG_ERR, "FileHandler: internal error: %s", err.to_string().c_str());
            return;
        }
    }
    //=========================================================================

    Server::Server(int port_p, unsigned nthreads_p, const std::string userid_str, const std::string &logfile_p, const std::string &loglevel_p)
        : port(port_p), _nthreads(nthreads_p), _logfilename(logfile_p), _loglevel(loglevel_p)
    {
        _ssl_port = -1;

        //
        // we use a semaphore object to control the number of threads
        //
        semname = "shttps";
        semname += std::to_string(port);
        _user_data = nullptr;
        running = false;
        _keep_alive_timeout = 20;

        int ll;
        if (_loglevel == "DEBUG") {
            ll = LOG_DEBUG;
        }
        else if (_loglevel == "INFO") {
            ll = LOG_INFO;
        }
        else if (_loglevel == "NOTICE") {
            ll = LOG_NOTICE;
        }
        else if (_loglevel == "WARN") {
            ll = LOG_WARNING;
        }
        else if (_loglevel == "ERROR") {
            ll = LOG_ERR;
        }
        else if (_loglevel == "CRITICAL") {
            ll = LOG_CRIT;
        }
        else if (_loglevel == "ALERT") {
            ll = LOG_ALERT;
        }
        else if (_loglevel == "EMER") {
            ll = LOG_EMERG;
        }
        else {
            ll = LOG_ERR;
        }
        openlog(loggername, LOG_CONS | LOG_PERROR, LOG_DAEMON);
        setlogmask(LOG_UPTO(ll));

        //
        // Her we check if we have to change to a different uid. This can only be done
        // if the server runs originally as root!
        //
        if (!userid_str.empty()) {
            if (getuid() == 0) { // must be root to setuid() !!
                struct passwd pwd, *res;

                size_t buffer_len = sysconf(_SC_GETPW_R_SIZE_MAX) * sizeof(char);
                auto buffer = make_unique<char[]>(buffer_len);
                getpwnam_r(userid_str.c_str(), &pwd, buffer.get(), buffer_len, &res);

                if (res != nullptr) {
                    if (setuid(pwd.pw_uid) == 0) {
                        int old_ll = setlogmask(LOG_MASK(LOG_INFO));
                        syslog(LOG_INFO, "Server will run as user %s (%d)", userid_str.c_str(), getuid());
                        setlogmask(old_ll);
                        if (setgid(pwd.pw_gid) == 0) {
                            int old_ll = setlogmask(LOG_MASK(LOG_INFO));
                            syslog(LOG_INFO, "Server will run with group-id %d", getgid());
                            setlogmask(old_ll);
                        }
                        else {
                            syslog(LOG_ERR, "setgid() failed! Reason: %m");
                        }
                    }
                    else {
                        syslog(LOG_ERR, "setgid() failed! Reason: %m");
                    }
                }
                else {
                    syslog(LOG_ERR, "Could not get uid of user %s: you must start Sipi as root", userid_str.c_str());
                }
            }
            else {
                syslog(LOG_ERR, "Could not get uid of user %s: you must start Sipi as root", userid_str.c_str());
            }
        }



#ifdef SHTTPS_ENABLE_SSL
        SSL_load_error_strings();
        SSL_library_init();
        OpenSSL_add_all_algorithms();
#endif

        ::sem_unlink(semname.c_str()); // unlink to be sure that we start from scratch
        _semaphore = ::sem_open(semname.c_str(), O_CREAT, 0x755, _nthreads);
        _semcnt = _nthreads;
    }
    //=========================================================================

#ifdef SHTTPS_ENABLE_SSL
    void Server::jwt_secret(const std::string &jwt_secret_p) {
        _jwt_secret = jwt_secret_p;
        auto secret_size = _jwt_secret.size();

        if (secret_size < 32) {
            for (int i = 0; i < (32 - secret_size); i++) {
                _jwt_secret.push_back('A' + i);
            }
        }
    }
    //=========================================================================
#endif



    RequestHandler Server::getHandler(Connection &conn, void** handler_data_p)
    {
        std::map<std::string, RequestHandler>::reverse_iterator item;

        size_t max_match_len = 0;
        std::string matching_path;
        RequestHandler matching_handler = nullptr;

        for (item = handler[conn.method()].rbegin(); item != handler[conn.method()].rend(); ++item) {
            size_t len = conn.uri().length() < item->first.length() ? conn.uri().length() : item->first.length();
            if (item->first == conn.uri().substr(0, len)) {
                if (len > max_match_len) {
                    max_match_len = len;
                    matching_path = item->first;
                    matching_handler = item->second;
                }
            }
        }
        if (max_match_len > 0) {
            *handler_data_p = handler_data[conn.method()][matching_path];
            return matching_handler;
        }
        return default_handler;
    }
    //=============================================================================


    static int prepare_socket(int port) {
        int sockfd;
        struct sockaddr_in serv_addr;

        sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            syslog(LOG_ERR, "Could not create socket: %m");
            exit(1);
        }

        int optval = 1;
        if (::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval) < 0) {
            syslog(LOG_ERR, "Could not set socket option: %m");
            exit(1);
        }

        /* Initialize socket structure */
        bzero((char *) &serv_addr, sizeof(serv_addr));

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        serv_addr.sin_port = htons(port);

        /* Now bind the host address using bind() call.*/
        if (::bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
            syslog(LOG_ERR, "Could not bind socket: %m");
            exit(1);
        }

        if (::listen(sockfd, SOMAXCONN) < 0) {
            syslog(LOG_ERR, "Could not listen on socket: %m");
            exit (1);
        }
        return sockfd;
    }
    //=========================================================================


    static void idle_add(pthread_t tid_p) {
        std::lock_guard<std::mutex> idle_mutex_guard(idlelock);

        idle_thread_ids.push_back(tid_p);
    }
    //=========================================================================

    static void idle_remove(pthread_t tid_p) {
        int index = 0;
        bool in_idle = false;

        std::lock_guard<std::mutex> idle_mutex_guard(idlelock);

        for (auto tid : idle_thread_ids) {
            if (tid == tid_p) {
                in_idle = true;
                break;
            }
            index++;
        }

        if (in_idle) {
            //
            // the vector is in idle state, remove it from the idle vector
            //
            idle_thread_ids.erase(idle_thread_ids.begin() + index);
        }
    }
    //=========================================================================

    static int close_socket(TData* tdata) {

#ifdef SHTTPS_ENABLE_SSL
        if (tdata->cSSL != nullptr) {
            int sstat;
            while ((sstat = SSL_shutdown(tdata->cSSL)) == 0);
            if (sstat < 0) {
                syslog(LOG_WARNING, "SSL socket error: shutdown of socket failed at [%s: %d] with error code %d", __file__, __LINE__, SSL_get_error(tdata->cSSL, sstat));
            }
            SSL_free(tdata->cSSL);
            tdata->cSSL = nullptr;
        }
#endif
        if (shutdown(tdata->sock, SHUT_RDWR) < 0) {
            syslog(LOG_DEBUG, "Debug: shutting down socket at [%s: %d]: %m failed (client terminated already?)", __file__, __LINE__);
        }

        if (close(tdata->sock) == -1) {
            syslog(LOG_DEBUG, "Debug: closing socket at [%s: %d]: %m failed (client terminated already?)", __file__, __LINE__);
        }

        return 0;
    }
    //=========================================================================


    /*!
     * Runs a request-handling thread.
     *
     * @param arg a pointer to a TData, which this function will delete before returning.
     * @return NULL.
     */
    static void *process_request(void *arg) {
        TData *tdata = static_cast<TData *>(arg);
        pthread_t my_tid = pthread_self();

        //
        // now we create the socket's SockStream
        //
        std::unique_ptr<SockStream> sockstream;
#ifdef SHTTPS_ENABLE_SSL
        if (tdata->cSSL != nullptr) {
            sockstream = make_unique<SockStream>(tdata->cSSL);
        }
        else {
            sockstream = make_unique<SockStream>(tdata->sock);
        }
#else
        sockstream = make_unique<SockStream>(tdata->sock);
#endif
        std::istream ins(sockstream.get());
        std::ostream os(sockstream.get());

        ThreadStatus tstatus;
        int keep_alive = 1;
        do {
#ifdef SHTTPS_ENABLE_SSL
            if (tdata->cSSL != nullptr) {
                tstatus = tdata->serv->processRequest(&ins, &os, tdata->peer_ip, tdata->peer_port, true, keep_alive);
            }
            else {
                tstatus = tdata->serv->processRequest(&ins, &os, tdata->peer_ip, tdata->peer_port, false, keep_alive);
            }
#else
            tstatus = tdata->serv->processRequest(&ins, &os, tdata->peer_ip, tdata->peer_port, false, keep_alive);
#endif

            if (tstatus == CLOSE) break; // it's CLOSE , let's get out of the loop

            //
            // tstatus is CONTINUE. Let's check if we got in the meantime a CLOSE message from the main...
            //
            pollfd readfds[2];
            readfds[0] = { tdata->commpipe_read, POLLIN, 0 };
            readfds[1] = { tdata->sock, POLLIN, 0 };
            if (poll(readfds, 2, 0) < 0) { // no blocking here!!!
                syslog(LOG_ERR, "Non-blocking poll failed at [%s: %d]", __file__, __LINE__);
                tstatus = CLOSE;
                break; // accept returned something strange – probably we want to shutdown the server
            }
            if (readfds[1].revents & POLLIN) {
                continue;
            }
            if (readfds[0].revents & POLLIN) { // something on the pipe from the main
                //
                // we got a message on the communication channel from the main thread...
                //
                if (Server::CommMsg::read(tdata->commpipe_read) != 0) {
                    keep_alive = -1;
                    break;
                }
                keep_alive = -1;
                if (readfds[1].revents & POLLIN) { // but we already have data...
                    continue; // continue loop
                }
                else {
                    break;
                }
            }

            //
            // we check if a new request is waiting for the semaphore which limits the number of threads
            //
            if (tdata->serv->semaphore_get() < 0) { //Another thread is waiting, give him a chance...
                keep_alive = -1;
                if (readfds[1].revents & POLLIN) { // but we already have data...
                    continue; // we have data, we will close after the next round...
                }
                else {
                    break;
                }
            }

            //
            // if we can continue and have a keep_alive, let's set the thread to idle...
            //
            idle_add(my_tid);

            //
            // use poll to wait...
            //
            readfds[0] = { tdata->commpipe_read, POLLIN, 0};
            readfds[1] = { tdata->sock, POLLIN, 0};
            if (poll(readfds, 2, keep_alive * 1000) < 0) {
                syslog(LOG_ERR, "Blocking poll failed at [%s: %d]", __file__, __LINE__);
                tstatus = CLOSE;
                idle_remove(my_tid);
                break; // accept returned something strange – probably we want to shutdown the server
            }
            if (!(readfds[0].revents | readfds[1].revents)) { // we got a timeout from poll
                //
                // timeout from poll
                //
                tstatus = CLOSE;
                idle_remove(my_tid);
                break;
            }
            if (readfds[0].revents & POLLIN) { // something on the pipe...
                //
                // we got a message on the communication channel from the main thread...
                //
                if (Server::CommMsg::read(tdata->commpipe_read) != 0) {
                    keep_alive = -1;
                    idle_remove(my_tid);
                    break;
                }
                keep_alive = -1;
                idle_remove(my_tid);
                if (readfds[1].revents & POLLIN) { // but we already have data...
                    continue; // continue loop
                }
                else {
                    break;
                }
            }
        } while (tstatus == CONTINUE);

        //
        // let's close the socket
        //
        close_socket(tdata);

        if (close(tdata->commpipe_read) == -1) {
            syslog(LOG_ERR, "Commpipe_write close error at [%s: %d]: %m", __file__, __LINE__);
        }

        int compipe_write = tdata->serv->get_thread_pipe(pthread_self());
        if (compipe_write > 0) {
            if (close (compipe_write) == -1) {
                syslog(LOG_ERR, "Commpipe_write close error at [%s: %d]: %m", __file__, __LINE__);
            }
        }
        else {
            syslog(LOG_DEBUG, "Thread to stop does not exist");
        }

        tdata->serv->remove_thread(pthread_self());
        tdata->serv->semaphore_leave();
        delete tdata;
        return nullptr;
    }
    //=========================================================================


    void Server::run()
    {
        // Start a thread just to catch signals sent to the server process.
        pthread_t sighandler_thread;
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        sigaddset(&set, SIGTERM);
        sigaddset(&set, SIGPIPE);

        int pthread_sigmask_result = pthread_sigmask(SIG_BLOCK, &set, nullptr);

        if (pthread_sigmask_result != 0) {
            syslog(LOG_ERR, "pthread_sigmask failed! (err=%d)", pthread_sigmask_result);
        }

        pthread_create(&sighandler_thread, nullptr, &sig_thread, (void *) this);

        int old_ll = setlogmask(LOG_MASK(LOG_INFO));
        syslog(LOG_INFO, "Starting shttps server with %d threads", _nthreads);
        setlogmask(old_ll);

        //
        // now we are adding the lua routes
        //
        for (auto & route : _lua_routes) {
            route.script = _scriptdir + "/" + route.script;
            addRoute(route.method, route.route, ScriptHandler, &(route.script));

            old_ll = setlogmask(LOG_MASK(LOG_INFO));
            syslog(LOG_INFO, "Added route %s with script %s", route.route.c_str(), route.script.c_str());
            setlogmask(old_ll);
        }

        _sockfd = prepare_socket(port);
        old_ll = setlogmask(LOG_MASK(LOG_INFO));
        syslog(LOG_INFO, "Server listening on port %d", port);
        setlogmask(old_ll);

        if (_ssl_port > 0) {
            _ssl_sockfd = prepare_socket(_ssl_port);
            old_ll = setlogmask(LOG_MASK(LOG_INFO));
            syslog(LOG_INFO, "Server listening on SSL port %d", _ssl_port);
            setlogmask(old_ll);
        }

        pipe(stoppipe); // ToDo: Errorcheck
        pthread_t thread_id;
        running = true;
        int count = 0;

        while (running) {
            int sock;

            pollfd readfds[3];
            int n_readfds = 0;
            readfds[0] = { _sockfd, POLLIN, 0}; n_readfds++;
            readfds[1] = {stoppipe[0], POLLIN, 0}; n_readfds++;

            if (_ssl_port > 0) {
                readfds[2] = {_ssl_sockfd, POLLIN, 0}; n_readfds++;
            }

            if (poll(readfds, n_readfds, -1) < 0) {
                syslog(LOG_ERR, "Blocking poll failed at [%s: %d]: %m", __file__, __LINE__);
                running = false;
                break;
            }

            count++;

            if (readfds[0].revents & POLLIN) {
                sock = _sockfd;
            } else if (readfds[1].revents & POLLIN) {
                sock = stoppipe[0];
                char buf[2];
                read(stoppipe[0], buf, 1);
                running = false;
                break;
            } else if ((_ssl_port > 0) && (readfds[2].revents & POLLIN)) {
                sock = _ssl_sockfd;
            } else {
                syslog(LOG_ERR, "Blocking poll failed at [%s: %d]: unknown error", __file__, __LINE__);
                running = false;
                break; // accept returned something strange – probably we want to shutdown the server
            }

            struct sockaddr_storage cli_addr;
            socklen_t cli_size = sizeof(cli_addr);
            int newsockfs = ::accept(sock, (struct sockaddr *) &cli_addr, &cli_size);

            if (newsockfs <= 0) {
                syslog(LOG_ERR, "Socket error  at [%s: %d]: %m", __file__, __LINE__);
                break; // accept returned something strange – probably we want to shutdown the server
            }

            //
            // get peer address
            //
            char client_ip[INET6_ADDRSTRLEN];
            int peer_port;

            if (cli_addr.ss_family == AF_INET) {
                struct sockaddr_in *s = (struct sockaddr_in *) &cli_addr;
                peer_port = ntohs(s->sin_port);
                inet_ntop(AF_INET, &s->sin_addr, client_ip, sizeof client_ip);
            } else if (cli_addr.ss_family == AF_INET6) { // AF_INET6
                struct sockaddr_in6 *s = (struct sockaddr_in6 *) &cli_addr;
                peer_port = ntohs(s->sin6_port);
                inet_ntop(AF_INET6, &s->sin6_addr, client_ip, sizeof client_ip);
            } else {
                peer_port = -1;
            }

            old_ll = setlogmask(LOG_MASK(LOG_INFO));
            syslog(LOG_INFO, "Accepted connection from %s", client_ip);
            setlogmask(old_ll);

            // Construct a TData for the thread that will handle the request. The TData will
            // be deleted by process_request() when it completes.
            TData* thread_data = new TData();
            thread_data->sock = newsockfs;
            thread_data->peer_ip = client_ip;
            thread_data->peer_port = peer_port;
            thread_data->serv = this;

#ifdef SHTTPS_ENABLE_SSL
            SSL *cSSL = nullptr;

            if (sock == _ssl_sockfd) {
                SSL_CTX *sslctx;
                try {
                    if ((sslctx = SSL_CTX_new(SSLv23_server_method())) == nullptr) {
                        syslog(LOG_ERR, "OpenSSL error: SSL_CTX_new() failed");
                        throw SSLError(__file__, __LINE__, "OpenSSL error: SSL_CTX_new() failed");
                    }
                    SSL_CTX_set_options(sslctx, SSL_OP_SINGLE_DH_USE);
                    if (SSL_CTX_use_certificate_file(sslctx, _ssl_certificate.c_str(), SSL_FILETYPE_PEM) != 1) {
                        std::string msg = "OpenSSL error: SSL_CTX_use_certificate_file(" + _ssl_certificate + ") failed";
                        syslog(LOG_ERR, "%s", msg.c_str());
                        throw SSLError(__file__, __LINE__, msg);
                    }
                    if (SSL_CTX_use_PrivateKey_file(sslctx, _ssl_key.c_str(), SSL_FILETYPE_PEM) != 1) {
                        std::string msg = "OpenSSL error: SSL_CTX_use_PrivateKey_file(" + _ssl_certificate + ") failed";
                        syslog(LOG_ERR, "%s", msg.c_str());
                        throw SSLError(__file__, __LINE__, msg);
                    }
                    if (!SSL_CTX_check_private_key(sslctx)) {
                        std::string msg = "OpenSSL error: SSL_CTX_check_private_key() failed";
                        syslog(LOG_ERR, "%s", msg.c_str());
                        throw SSLError(__file__, __LINE__, msg);
                    }
                    if ((cSSL = SSL_new(sslctx)) == nullptr) {
                        std::string msg = "OpenSSL error: SSL_new() failed";
                        syslog(LOG_ERR, "%s", msg.c_str());
                        throw SSLError(__file__, __LINE__, msg);
                    }
                    if (SSL_set_fd(cSSL, newsockfs) != 1) {
                        std::string msg = "OpenSSL error: SSL_set_fd() failed";
                        syslog(LOG_ERR, "%s", msg.c_str());
                        throw SSLError(__file__, __LINE__, msg);
                    }

                    //Here is the SSL Accept portion.  Now all reads and writes must use SS
                    int suc;
                    if ((suc = SSL_accept(cSSL)) <= 0) {
                        std::string msg = "OpenSSL error: SSL_accept() failed";
                        syslog(LOG_ERR, "%s", msg.c_str());
                        throw SSLError(__file__, __LINE__, msg);
                    }
                } catch (SSLError &err) {
                    syslog(LOG_ERR, "%s", err.to_string().c_str());
                    int sstat;
                    while ((sstat = SSL_shutdown(cSSL)) == 0);
                    if (sstat < 0) {
                        syslog(LOG_WARNING, "SSL socket error: shutdown (2) of socket failed: %d", SSL_get_error(cSSL, sstat));
                    }
                    SSL_free(cSSL);
                    cSSL = nullptr;
                }
            }

            thread_data->cSSL = cSSL;
#endif

            if (semaphore_get() <= 0) {
                //
                // we would be blocked by the semaphore... Get an idle thread...
                //
                {
                    std::unique_lock<std::mutex> idle_mutex_guard(idlelock);

                    if (idle_thread_ids.size() > 0) {
                        pthread_t tid = idle_thread_ids.front();
                        idle_mutex_guard.unlock();

                        int pipe_id = get_thread_pipe(tid);

                        if (pipe_id > 0) {
                            Server::CommMsg::send(pipe_id);
                        } else {
                            syslog(LOG_DEBUG, "The thread to stop no longer exists");
                        }
                    }
                }
            }

            semaphore_wait();
            int commpipe[2];

            if (socketpair(PF_LOCAL, SOCK_STREAM, 0, commpipe) != 0) {
                syslog(LOG_WARNING, "Creating pipe failed at [%s: %d]: %m", __file__, __LINE__);
                running = false;
                break;
            }

            thread_data->commpipe_read = commpipe[1]; // read end;

            pthread_attr_t tattr;
            pthread_attr_init(&tattr);

            if (pthread_create(&thread_id, &tattr, process_request, (void *) thread_data) < 0) {
                syslog(LOG_ERR, "Could not create thread at [%s: %d]: %m", __file__, __LINE__);
                running = false;
                break;
            }
#ifdef SHTTPS_ENABLE_SSL
            if (thread_data->cSSL != nullptr) {
                add_thread(thread_id, commpipe[0], thread_data->sock, thread_data->cSSL);
            }
            else {
                add_thread(thread_id, commpipe[0], thread_data->sock);
            }
#else
            add_thread(thread_id, commpipe[0], thread_data->sock);
#endif
        }

        old_ll = setlogmask(LOG_MASK(LOG_INFO));
        syslog(LOG_INFO, "Server shutting down");
        setlogmask(old_ll);
        std::vector<pthread_t> threads_to_join;
        threads_to_join.push_back(sighandler_thread);

        {
            std::lock_guard<std::mutex> thread_mutex_guard(threadlock);

            // Send the close message to all running threads.
            for (auto const &tid : thread_ids) {
                threads_to_join.push_back(tid.first);
                Server::CommMsg::send(tid.second.commpipe_write);
            }
        }

        close(stoppipe[0]);
        close(stoppipe[1]);

        // We have closed all sockets, now wait for the threads to terminate.

        for (auto const &thread_to_join : threads_to_join) {
            int err = pthread_join(thread_to_join, nullptr);

            if (err != 0) {
                syslog(LOG_ERR, "pthread_join failed with error code %d", err);
            }
        }

        // Close the semaphore.
        ::sem_close(_semaphore);

        // std::cerr << "signal_result is " << signal_result << std::endl;
   }
    //=========================================================================


    void Server::addRoute(Connection::HttpMethod method_p, const std::string &path_p, RequestHandler handler_p, void *handler_data_p)
    {
        handler[method_p][path_p] = handler_p;
        handler_data[method_p][path_p] = handler_data_p;
    }
    //=========================================================================


    ThreadStatus Server::processRequest(std::istream *ins, std::ostream *os, std::string &peer_ip, int peer_port, bool secure, int &keep_alive)
    {
        if (_tmpdir.empty()) {
            syslog(LOG_WARNING, "_tmpdir is empty");
            throw Error(__file__, __LINE__, "_tmpdir is empty");
        }

        if (ins->eof() || os->eof()) return CLOSE;

        try {
            Connection conn(this, ins, os, _tmpdir);

            if (keep_alive <= 0) {
                conn.keepAlive(false);
            }
            keep_alive = conn.setupKeepAlive(_keep_alive_timeout);

            conn.peer_ip(peer_ip);
            conn.peer_port(peer_port);
            conn.secure(secure);


            if (conn.resetConnection()) {
                if (conn.keepAlive()) {
                    return CONTINUE;
                }
                else {
                    return CLOSE;
                }
            }

            //
            // Setting up the Lua server
            //
            LuaServer luaserver(conn, _initscript, true);
            luaserver.setLuaPath(_scriptdir + "/?.lua"); // add the script dir to the standard search path fpr lua packages
            for (auto &global_func : lua_globals) {
                global_func.func(luaserver.lua(), conn, global_func.func_dataptr);
            }

            void *hd = nullptr;
            try {
                RequestHandler handler = getHandler(conn, &hd);
                handler(conn, luaserver, _user_data, hd);
            } catch (InputFailure iofail) {
                syslog(LOG_ERR, "Possibly socket closed by peer");
                return CLOSE; // or CLOSE ??
            }

            if (!conn.cleanupUploads()) {
                syslog(LOG_ERR, "Cleanup of uploaded files failed");
            }

            if (conn.keepAlive()) {
                return CONTINUE;
            } else {
                return CLOSE;
            }
        }
        catch (InputFailure iofail) { // "error" is thrown, if the socket was closed from the main thread...
            syslog(LOG_DEBUG, "Socket connection: timeout or socket closed from main");
            return CLOSE;
        }
        catch(Error &err) {
            syslog(LOG_WARNING, "Internal server error: %s", err.to_string().c_str());
            try {
                *os << "HTTP/1.1 500 INTERNAL_SERVER_ERROR\r\n";
                *os << "Content-Type: text/plain\r\n";
                std::stringstream ss;
                ss << err;
                *os << "Content-Length: " << ss.str().length() << "\r\n\r\n";
                *os << ss.str();
            } catch (InputFailure iofail) {
                syslog(LOG_DEBUG, "Possibly socket closed by peer");
            }
            return CLOSE;
        }
    }
    //=========================================================================

    void Server::add_thread(pthread_t thread_id_p, int commpipe_write_p, int sock_id) {
        std::lock_guard<std::mutex> thread_mutex_guard(threadlock);
#ifdef SHTTPS_ENABLE_SSL
        GenericSockId sid = {sock_id, nullptr, commpipe_write_p};
#else
        GenericSockId sid = {sock_id, commpipe_write_p};
#endif
        thread_ids[thread_id_p] = sid;
    }
    //=========================================================================

#ifdef SHTTPS_ENABLE_SSL
    void Server::add_thread(pthread_t thread_id_p, int commpipe_write_p, int sock_id, SSL *cSSL) {
        std::lock_guard<std::mutex> thread_mutex_guard(threadlock);

        GenericSockId sid = {sock_id, cSSL, commpipe_write_p};
        thread_ids[thread_id_p] = sid;
    }
#endif
    //=========================================================================

    int Server::get_thread_sock(pthread_t thread_id_p) {
        std::lock_guard<std::mutex> thread_mutex_guard(threadlock);

        try {
            return thread_ids.at(thread_id_p).sid;
        } catch (const std::out_of_range& oor) {
            return -1;
        }
    }
    //=========================================================================

    int Server::get_thread_pipe(pthread_t thread_id_p) {
        std::lock_guard<std::mutex> thread_mutex_guard(threadlock);

        try {
            return thread_ids.at(thread_id_p).commpipe_write;
        }
        catch (const std::out_of_range& oor) {
            return -1;
        }
    }
    //=========================================================================


#ifdef SHTTPS_ENABLE_SSL
    SSL *Server::get_thread_ssl(pthread_t thread_id_p) {
        std::lock_guard<std::mutex> thread_mutex_guard(threadlock);

        try {
            return thread_ids.at(thread_id_p).ssl_sid;
        }
        catch (const std::out_of_range& oor) {
            return nullptr;
        }
    }
    //=========================================================================
#endif


    void Server::remove_thread(pthread_t thread_id_p) {
        int index = 0;
        bool in_idle = false;

        {
            std::lock_guard<std::mutex> idle_mutex_guard(idlelock);

            for (auto tid : idle_thread_ids) {
                if (tid == thread_id_p) {
                    in_idle = true;
                    break;
                }
                index++;
            }

            if (in_idle) {
                //
                // the thread is in idle state, remove it from the idle vector
                //
                idle_thread_ids.erase(idle_thread_ids.begin() + index);
            }
        }

        {
            std::lock_guard<std::mutex> thread_mutex_guard(threadlock);
            thread_ids.erase(thread_id_p);
        }
    }
    //=========================================================================

    void Server::debugmsg(const std::string &msg) {
        std::lock_guard<std::mutex> debug_mutex_guard(debugio);

        std::cerr << msg << std::endl;
    }
    //=========================================================================

}
