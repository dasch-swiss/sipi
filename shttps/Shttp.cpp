/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <string>
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <csignal>
#include <utility>

#include "Error.h"
#include "Server.h"
#include "LuaServer.h"
#include "Logger.h"

shttps::Server *serverptr = nullptr;

static sig_t old_sighandler;
static sig_t old_broken_pipe_handler;

static void sighandler(int sig) {
    if (serverptr != nullptr) {
        log_info("Got SIGINT, stopping server");
        serverptr->stop();
    } else {
        exit(0);
    }
}


/*LUA TEST****************************************************************************/
static int lua_gaga(lua_State *L) {
    lua_getglobal(L, shttps::luaconnection); // push onto stack
    shttps::Connection *conn = (shttps::Connection *) lua_touserdata(L, -1); // does not change the stack
    lua_remove(L, -1); // remove from stack

    int top = lua_gettop(L);

    for (int i = 1; i <= top; i++) {
        const char *str = lua_tostring(L, i);
        if (str != nullptr) {
            conn->send("GAGA: ", 5);
            conn->send(str, strlen(str));
        }
    }
    return 0;
}
//=========================================================================


/*!
 * Just some testing testing the extension of lua, not really doing something useful
 */
static void new_lua_func(lua_State *L, shttps::Connection &conn, void *user_data) {
    lua_pushcfunction(L, lua_gaga);
    lua_setglobal(L, "gaga");
}
//=========================================================================

/*LUA TEST****************************************************************************/


void RootHandler(shttps::Connection &conn, shttps::LuaServer &luaserver, void *user_data, void *dummy) {
    conn.setBuffer();
    std::vector <std::string> headers = conn.header();
    for (unsigned i = 0; i < headers.size(); i++) {
        conn << headers[i] << " : " << conn.header(headers[i]) << "\n";
    }
    conn << "URI: " << conn.uri() << "\n";
    conn << "It works!" << shttps::Connection::flush_data;
    return;
}


void TestHandler(shttps::Connection &conn, shttps::LuaServer &luaserver, void *user_data, void *dummy) {
    conn.setBuffer();
    conn.setChunkedTransfer();

    std::vector <std::string> headers = conn.header();
    for (unsigned i = 0; i < headers.size(); i++) {
        std::cerr << headers[i] << " : " << conn.header(headers[i]) << std::endl;
    }

    if (!conn.getParams("gaga").empty()) {
        std::cerr << "====> gaga = " << conn.getParams("gaga") << std::endl;
    }

    conn.header("Content-Type", "text/html; charset=utf-8");
    conn << "<html><head>";
    conn << "<title>SIPI TEST (chunked transfer)</title>";
    conn << "</head>" << shttps::Connection::flush_data;

    conn << "<body><h1>SIPI TEST (chunked transfer)</h1>";
    conn << "<p>Dies ist ein kleiner Text</p>";
    conn << "</body></html>" << shttps::Connection::flush_data;
    return;
}


int main(int argc, char *argv[]) {
    std::string userid;
    int port = 4711;
    int ssl_port = -1;
    std::string ssl_certificate;
    std::string ssl_key;
    std::string jwt_secret;
    int nthreads = 4;
    std::string configfile;
    std::string docroot;
    std::string tmpdir;
    std::string scriptdir;
    std::vector <shttps::LuaRoute> routes;
    std::pair <std::string, std::string> filehandler_info;
    int keep_alive = 20;
    size_t max_post_size = 0;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-p") == 0) || (strcmp(argv[i], "-port") == 0)) {
            i++;
            if (i < argc) port = atoi(argv[i]);
        } else if ((strcmp(argv[i], "-c") == 0) || (strcmp(argv[i], "-config") == 0)) {
            i++;
            if (i < argc) configfile = argv[i];
        } else if ((strcmp(argv[i], "-d") == 0) || (strcmp(argv[i], "-docroot") == 0)) {
            i++;
            if (i < argc) docroot = argv[i];
        } else if ((strcmp(argv[i], "-t") == 0) || (strcmp(argv[i], "-tmpdir") == 0)) {
            i++;
            if (i < argc) tmpdir = argv[i];
        } else if ((strcmp(argv[i], "-n") == 0) || (strcmp(argv[i], "-nthreads") == 0)) {
            i++;
            if (i < argc) nthreads = atoi(argv[i]);
        } else if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "-help") == 0) ||
                   (strcmp(argv[i], "--help") == 0)) {
            std::cerr << "usage:" << std::endl;
            std::cerr
                    << "shttp-test [-p|-port <int def=4711>] [-c|-config <filename>] [-d|-docroot <path>] [-t|-tmpdir <path>] [-n|-nthreads <int def=4>]"
                    << std::endl << std::endl;
            return 0;
        }
    }

    /*
     * Form if config file:
     *
     * shttps = {
     *    docroot = '/Volumes/data/shttps-docroot',
     *    tmpdir = '/tmp',
     *    port = 8080,
     *    nthreads = 16
     * }
     */
    if (!configfile.empty()) {
        try {
            shttps::LuaServer luacfg = shttps::LuaServer(configfile);
            userid = luacfg.configString("shttps", "userid", "");
            port = luacfg.configInteger("shttps", "port", 4711);
            ssl_port = luacfg.configInteger("shttps", "ssl_port", -1);
            ssl_certificate = luacfg.configString("shttps", "ssl_certificate", "");
            ssl_key = luacfg.configString("shttps", "ssl_key", "");
            jwt_secret = luacfg.configString("shttps", "jwt_secret", "0123456789ABCDEF0123456789ABCDEF");
            docroot = luacfg.configString("shttps", "docroot", ".");
            tmpdir = luacfg.configString("shttps", "tmpdir", "/tmp");
            scriptdir = luacfg.configString("shttps", "scriptdir", "./scripts");
            nthreads = luacfg.configInteger("shttps", "nthreads", 4);
            keep_alive = luacfg.configInteger("shttps", "keep_alive", 20);
            max_post_size = luacfg.configInteger("shttps", "max_post_size", 0);
            std::string s;
            std::string initscript = luacfg.configString("shttps", "initscript", s);
            routes = luacfg.configRoute("routes");
        } catch (shttps::Error &err) {
            std::cerr << err << std::endl;
        }
    }

    shttps::Server server(port, nthreads, userid); // instantiate the server
    server.ssl_port(ssl_port); // set the secure connection port (-1 means no ssl socket)
    if (!ssl_certificate.empty()) server.ssl_certificate(ssl_certificate);
    if (!ssl_key.empty()) server.ssl_key(ssl_key);
    server.jwt_secret(jwt_secret);
    server.tmpdir(tmpdir); // set the directory for storing temporary files during upload
    server.scriptdir(scriptdir); // set the directory where the Lua scripts are found for the "Lua"-routes
    server.max_post_size(max_post_size); // set the maximal post size
    server.luaRoutes(routes);
    server.keep_alive_timeout(keep_alive); // set the keep alive timeout
    server.add_lua_globals_func(sqliteGlobals, &server);
    server.add_lua_globals_func(new_lua_func); // add new lua function "gaga"

    //
    // now we set the routes for the normal HTTP server file handling
    //
    if (!docroot.empty()) {
        filehandler_info.first = "/";
        filehandler_info.second = docroot;
        server.addRoute(shttps::Connection::GET, "/", shttps::FileHandler, &filehandler_info);
        server.addRoute(shttps::Connection::POST, "/", shttps::FileHandler, &filehandler_info);
    }

    //
    // Test handler (should be removed for production system)
    //
    server.addRoute(shttps::Connection::GET, "/test", TestHandler, nullptr);

    serverptr = &server;
    old_sighandler = signal(SIGINT, sighandler);
    old_broken_pipe_handler = signal(SIGPIPE, SIG_IGN);

    server.run();
    std::cerr << "SERVER HAS FINISHED ITS SERVICE" << std::endl;
}
