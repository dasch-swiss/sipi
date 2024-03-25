/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * \brief Implements a simple HTTP server.
 *
 */
#ifndef __shttp_server_h
#define __shttp_server_h


#include <csignal>
#include <map>
#include <mutex>
#include <queue>
#include <vector>

#include <poll.h>
#include <pthread.h>//for threading , link with lpthread
#include <semaphore.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include <atomic>
#include <iostream>
#include <netdb.h>// Needed for the socket functions
#include <sstream>// std::stringstream

#include "openssl/bio.h"
#include "openssl/err.h"
#include "openssl/ssl.h"

#include "Error.h"
#include "Global.h"

#include "Connection.h"
#include "LuaServer.h"

#include "SocketControl.h"
#include "ThreadControl.h"


#include "lua.hpp"

/*
 * How to create a self-signed certificate
 *
 * openssl genrsa -out key.pem 2048
 * openssl req -new -key key.pem -out csr.pem
 * openssl req -x509 -days 365 -key key.pem -in csr.pem -out certificate.pem
 */

namespace shttps {


typedef void (*RequestHandler)(Connection &, LuaServer &, void *, void *);

extern void file_handler(shttps::Connection &conn, LuaServer &lua, void *user_data, void *handler_data);

typedef enum { CONTINUE, CLOSE } ThreadStatus;


/*!
 * \brief Implements a simple, even primitive HTTP server with routes and handlers
 *
 * Implementation of a simple, almost primitive, multithreaded HTTP server. The user can define for
 * request types and different paths handlers which will be called. The handler gets an Connection
 * instance which will be used to receive and send data.
 *
 *     void MirrorHandler(shttps::Connection &conn, void *user_data)
 *     {
 *         conn.setBuffer();
 *         std::vector<std::string> headers = conn.header();
 *         if (!conn.query("html").empty()) {
 *             conn.header("Content-Type", "text/html; charset=utf-8");
 *             conn << "<html><head>";
 *             conn << "<title>Mirror, mirror – on the wall...</title>";
 *             conn << "</head>" << shttps::Connection::flush_data;
 *
 *             conn << "<body><h1>Header fields</h1>";
 *             conn << "<table>";
 *             conn << <tr><th>Fieldname</th><th>Value</th></tr>";
 *             for (unsigned i = 0; i < headers.size(); i++) {
 *               conn << "<tr><td>" << headers[i] << "</td><td>" << conn.header(headers[i]) << "</td></tr>";
 *             }
 *             conn << "</table>"
 *             conn << "</body></html>" << shttps::Connection::flush_data;
 *         }
 *         else {
 *             conn.header("Content-Type", "text/plain; charset=utf-8");
 *             for (unsigned i = 0; i < headers.size(); i++) {
 *                 conn << headers[i] << " : " << conn.header(headers[i]) << "\n";
 *             }
 *         }
 *     }
 *
 *     shttps::Server server(4711);
 *     server.add_route(shttps::Connection::GET, "/", RootHandler);
 *     server.add_route(shttps::Connection::GET, "/mirror", MirrorHandler);
 *     server.run();
 *
 */
class Server
{
  /*!
   * Struct to hold Global Lua function and associated userdata
   */
  using GlobalFunc = struct
  {
    LuaSetGlobalsFunc func;
    void *func_dataptr;
  };

  /*!
   * Error handling class for SSL functions
   */
  class SSLError : Error
  {
  protected:
    SSL *cSSL;

  public:
    explicit SSLError(const char *msg,
      SSL *cSSL_p = nullptr,
      const std::source_location &loc = std::source_location::current())
      : Error(msg, 0, loc), cSSL(cSSL_p){};

    explicit SSLError(const std::string &msg,
      SSL *cSSL_p = nullptr,
      const std::source_location &loc = std::source_location::current())
      : Error(msg, 0, loc), cSSL(cSSL_p){};

    [[nodiscard]]  std::string to_string() const override
    {
      std::stringstream ss;
      ss << "SSL-ERROR at [" << this->getFile() << ": " << this->getLine() << "] ";
      BIO *bio = BIO_new(BIO_s_mem());
      ERR_print_errors(bio);
      char *buf = nullptr;
      long n = BIO_get_mem_data(bio, &buf);
      if (n > 0) { ss << buf << " : "; }
      BIO_free(bio);
      // ss << "Description: " << message;
      return ss.str();
    };
  };

public:
  /*!
   * Used to send a message between main thread and worker threads. A pipe is
   * used and the worker theads use poll to detect an incoming message.
   */
  class CommMsg
  {
  public:
    static int send(int pipe_id)
    {
      if ((::send(pipe_id, "X", 1, 0)) != 1) { return -1; }
      return 0;
    };

    static int read(int pipe_id)
    {
      char c;
      if (::read(pipe_id, &c, 1) != 1) { return -1; }
      return 0;
    };
  };
  //=========================================================================

private:
  int _port;//!< listening Port for server
  int _ssl_port;//!< listening port for openssl
  int _sockfd;//!< socket id
  int _ssl_sockfd;//!< SSL socket id

  std::string _ssl_certificate;//!< Path to SSL certificate
  std::string _ssl_key;//!< Path to SSL certificate
  std::string _jwt_secret;

  int stoppipe[2];

  std::string _tmpdir;//!< path to directory, where uplaods are being stored
  std::string _scriptdir;//!< Path to directory, where scripts for the "Lua"-routes are found
  unsigned _nthreads;//!< maximum number of parallel threads for processing requests
  std::map<pthread_t, SocketControl::SocketInfo> thread_ids;//!< Map of active worker threads
  int _keep_alive_timeout;
  bool running;//!< Main runloop should keep on going
  std::map<std::string, RequestHandler> handler[9];// request handlers for the different 9 request methods
  std::map<std::string, void *> handler_data[9];// request handlers for the different 9 request methods
  void *_user_data;//!< Some opaque user data that can be given to the Connection (for use within the handler)
  std::string _initscript;
  std::vector<shttps::LuaRoute> _lua_routes;//!< This vector holds the routes that are served by lua scripts
  std::vector<GlobalFunc> lua_globals;
  size_t _max_post_size;

  RequestHandler get_handler(Connection &conn, void **handler_data_p);

  SocketControl::SocketInfo accept_connection(int sock, bool ssl = false);

  std::string _logfilename;
  std::string _loglevel;

public:
  /*!
   * Create a server listening on the given port with the maximal number of threads
   *
   * \param[in] port_p Listening port of HTTP server
   * \param[in] nthreads_p Maximal number of parallel threads serving the requests
   */
  Server(int port_p,
    unsigned nthreads_p = 4,
    const std::string userid_str = "",
    const std::string &logfile_p = "shttps.log",
    const std::string &loglevel_p = "DEBUG");

  inline int port(void) { return _port; }

  /*!
   * Sets the port number for the SSL socket
   *
   * \param[in] ssl_port_p Port number
   */
  inline void ssl_port(int ssl_port_p) { _ssl_port = ssl_port_p; }

  /*!
   * Gets the port number of the SSL socket
   *
   * \returns SSL socket portnumber
   */
  inline int ssl_port(void) { return _ssl_port; }

  /*!
   * Sets the file path to the SSL certficate necessary for OpenSSL to work
   *
   * \param[in] path File path to th SSL certificate
   */
  inline void ssl_certificate(const std::string &path) { _ssl_certificate = path; }

  /*!
   * Returns the path of the SSL certificate
   *
   * \returns Path to the SSL certificate
   */
  inline std::string ssl_certificate(void) { return _ssl_certificate; }

  /*!
   * Sets the path to the SSP key
   *
   * \param[in] path Path to the SSL key necessary for OpenSSL to work
   */
  inline void ssl_key(const std::string &path) { _ssl_key = path; }

  /*!
   * Returns the path of the OpenSSL key
   *
   * \returns Path to the OpenSSL key
   */
  inline std::string ssl_key(void) { return _ssl_key; }

  /*!
   * Sets the secret for the generation JWT's (JSON Web Token). It must be a string
   * of length 32, since we're using currently SHA256 encoding.
   *
   * \param[in] jwt_secret_p String with 32 characters for the key for JWT's
   */
  void jwt_secret(const std::string &jwt_secret_p);

  /*!
   * Returns the secret used for JWT's
   *
   * \returns String of length 32 with the secret used for JWT's
   */
  inline std::string jwt_secret(void) { return _jwt_secret; }

  /*!
   * Returns the maximum number of parallel threads allowed
   *
   * \returns Number of parallel threads allowed
   */
  inline unsigned nthreads(void) { return _nthreads; }

  /*!
   * Return the path where to store temporary files (for uploads)
   *
   * \returns Path to directory for temporary files
   */
  inline std::string tmpdir(void) { return _tmpdir; }

  /*!
   * set the path to the  directory where to store temporary files during uploads
   *
   * \param[in] path to directory without trailing '/'
   */
  inline void tmpdir(const std::string &tmpdir_p) { _tmpdir = tmpdir_p; }

  /*!
   * Return the path where the Lua scripts for "Lua"-routes are found
   *
   * \returns Path to directory for script directory
   */
  inline std::string scriptdir(void) { return _scriptdir; }

  /*!
   * set the path to the  directory where to store temporary files during uploads
   *
   * \param[in] path to directory without trailing '/'
   */
  inline void scriptdir(const std::string &scriptdir_p) { _scriptdir = scriptdir_p; }

  /*!
   * Get the maximum size of a post request in bytes
   *
   * \returns Actual maximal size of  post request
   */
  inline size_t max_post_size(void) { return _max_post_size; }

  /*!
   * Set the maximal size of a post request
   *
   * \param[in] sz Maximal size of a post request in bytes
   */
  inline void max_post_size(size_t sz) { _max_post_size = sz; }

  /*!
   * Returns the routes defined for being handletd by Lua scripts
   *
   * \returns Vector of Lua route infos
   */
  std::vector<shttps::LuaRoute> luaRoutes() const { return _lua_routes; }

  /*!
   * set the routes that should be handled by Lua scripts
   *
   * \param[in] Vector of lua route infos
   */
  void luaRoutes(const std::vector<shttps::LuaRoute> &lua_routes_p) { _lua_routes = lua_routes_p; }

  /*!
   * Set the loglevel
   *
   * \param[in] loglevel_p set the loglevel
   */
  void loglevel(int loglevel_p) { setlogmask(LOG_UPTO(loglevel_p)); }

  /*!
   * Run the server handling requests in an infinite loop
   */
  virtual void run();

  /*!
   * Set the default value for the keep alive timout. This is the time in seconds
   * a HTTP connection (socket) remains up without action before being closed by
   * the server. A keep-alive header will change this value
   */
  void keep_alive_timeout(int keep_alive_timeout) { _keep_alive_timeout = keep_alive_timeout; }

  /*!
   * Returns the default keep alive timeout
   *
   * \returns Keep alive timeout in seconds
   */
  int keep_alive_timeout() const { return _keep_alive_timeout; }

  /*
  void add_thread(pthread_t thread_id_p, int commpipe_write_p, int sock_id);


  void thread_push(pthread_t thread_id_p, int commpipe_write_p, int sock_id, SSL *cSSL);

  int get_thread_sock(pthread_t thread_id_p);

  int get_thread_pipe(pthread_t thread_id_p);

  SSL *get_thread_ssl(pthread_t thread_id_p);


  void remove_thread(pthread_t thread_id_p);
*/
  /*!
   * Sets the path to the initialization script (lua script) which is executed for each request
   *
   * \param[in] initscript_p Path of initialization script
   */
  inline void initscript(const std::string &initscript_p)
  {
    std::ifstream t(initscript_p);
    if (t.fail()) { throw Error("initscript \"" + initscript_p + "\" not found!"); }

    t.seekg(0, std::ios::end);
    _initscript.reserve(t.tellg());
    t.seekg(0, std::ios::beg);

    _initscript.assign((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
  }

  /*!
   * adds a function which is called before processing each request to initialize
   * special Lua variables and add special Lua functions
   *
   * \param[in] func C++ function which extends the Lua
   */
  inline void add_lua_globals_func(LuaSetGlobalsFunc func, void *user_data = nullptr)
  {
    GlobalFunc gf;
    gf.func = func;
    gf.func_dataptr = user_data;
    lua_globals.push_back(gf);
  }

  /*!
   * Add a request handler for the given request method and route
   *
   * \param[in] method_p Request method (GET, PUT, POST etc.)
   * \param[in] path_p Route that this handler should serve
   * \param[in] handler_p Handler function which serves this method/route combination.
   *            The handler has the form
   *
   *      void (*RequestHandler)(Connection::Connection &, void *);
   *
   * \param[in] handler_data_p Pointer to arbitrary data given to the handler when called
   *
   */
  void add_route(Connection::HttpMethod method_p,
    const std::string &path_p,
    RequestHandler handler_p,
    void *handler_data_p = nullptr);

  /*!
   * Process a request... (Eventually should be private method)
   *
   * \param[in] sock Socket id
   * \param[in] peer_ip String containing IP (IP4 or IP6) of client/peer
   * \param[in] peer_port Port number of peer/client
   */
  ThreadStatus process_request(std::istream *ins,
    std::ostream *os,
    std::string &peer_ip,
    int peer_port,
    bool secure,
    int &keep_alive,
    bool socket_reuse = false);

  /*!
   * Return the user data that has been added previously
   */
  inline void *user_data(void) { return _user_data; }

  /*!
   * Add a pointer to user data which will be made available to the handler
   *
   * \param[in] User data
   */
  inline void user_data(void *user_data_p) { _user_data = user_data_p; }

  static void debugmsg(const int line, const std::string &msg);

  /*!
   * Stop the server gracefully (all destructors are called etc.) and the
   * cache file is updated. This function is asynchronous-safe, so it may be called
   * from within a signal handler.
   */
  inline void stop(void)
  {
    // POSIX declares write() to be asynchronous-safe.
    // See
    // https://www.securecoding.cert.org/confluence/display/c/SIG30-C.+Call+only+asynchronous-safe+functions+within+signal+handlers
    SocketControl::SocketInfo sockid(SocketControl::EXIT, SocketControl::STOP_SOCKET);
    SocketControl::send_control_message(stoppipe[1], sockid);

    debugmsg(__LINE__, "Sent stop message to stoppipe[1]=" + std::to_string(stoppipe[1]));
  }
};

}// namespace shttps

#endif
