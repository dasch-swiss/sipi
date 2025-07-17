/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * \brief Implements the handling of the http server connections.
 *
 */
#ifndef __shttp_connection_h
#define __shttp_connection_h

#include <algorithm>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "Error.h"


namespace shttps {

extern const size_t max_headerline_len;

typedef enum { INPUT_READ_FAIL = -1, OUTPUT_WRITE_FAIL = -2 } InputFailure;

class Server;

/*!
 * Function which splits a string at the first occurrence of a given character
 *
 * \params[in] str String to be split
 * \param[in] c The character which indicates the split position in the string
 *
 * \returns A pair of strings
 */
extern std::pair<std::string, std::string> strsplit(const std::string &str, const char c);

/*!
 * Function to read a line from the HTTP stream which is terminated by either
 * "\n" or "\r\n"
 *
 * \param[in] is Input stream (HTTP socket stream)
 * \param[out] t String containing the line
 *
 * \returns Number of bytes read
 */
extern size_t safeGetline(std::istream &is, std::string &t, size_t max_n = 0);

/*!
 * Function to parse the options of a HTTP header line
 *
 * \param[in] options Options-part of the string
 * \param[in] form_encoded Has to be set to true, if it is form_encoded data [default: false]
 * \param[in] sep Separator character between options [default: ';']
 *
 * \returns map of options (all names converted to lower case!)
 */
extern std::unordered_map<std::string, std::string>
  parse_header_options(const std::string &options, bool form_encoded = false, char sep = ';');

/*!
 * urldecode is used to decode an according to the HTTP-standard urlencoded string
 *
 * \param[in] src Encoded string
 * \param[in] form_encoded Boolean which must be true if the string has been part
 * of a form
 *
 * \returns Decoded string
 */
extern std::string urldecode(const std::string &src, bool form_encoded = false);

/*!
 * Encode a string (value) acording the the rules of urlencoded strings
 *
 * \param[in] value String to be encoded
 *
 * \returns Encoded string
 */
extern std::string urlencode(const std::string &value);

/*!
 * convert a string to all lowercase characters. The string should contain
 * only ascii characters. The outcome of non-ascii characters is undefined.
 *
 * \param[in] str Input string with mixed case
 *
 * \returns String converted to all lower case
 */
inline void asciitolower(std::string &str) { std::transform(str.begin(), str.end(), str.begin(), ::tolower); }

/*!
 * convert a string to all uppercase characters. The string should contain
 * only ascii characters. The outcome of non-ascii characters is undefined.
 *
 * \param[in] str Input string with mixed case
 *
 * \returns String converted to all upper case
 */
inline void asciitoupper(std::string &str) { std::transform(str.begin(), str.end(), str.begin(), ::toupper); }

/*!
 * This is a class used to represent the possible options of a HTTP cookie
 */
class Cookie
{
private:
  std::string _name;
  std::string _value;
  std::string _path;
  std::string _domain;
  std::string _expires;
  bool _secure;
  bool _http_only;

public:
  /*!
   * Constructor of the cookie class
   *
   * \param[in] name_p Name of the cookie
   * \param[in] value_p Value of the Cookie
   */
  inline Cookie(const std::string &name_p, const std::string value_p)
    : _name(name_p), _value(value_p), _secure(true), _http_only(false)
  {}

  /*!
   * Getter for the name
   */
  inline std::string name(void) const { return _name; }

  /*!
   * Setter for the name
   *
   * \param[in] name_p Name of the Cookie
   */
  inline void name(const std::string &name_p) { _name = name_p; }

  inline std::string value(void) const { return _value; }

  inline void value(const std::string &value_p) { _value = value_p; }

  inline std::string path(void) const { return _path; }

  inline void path(const std::string &path_p) { _path = path_p; }

  inline std::string domain(void) const { return _domain; }

  inline void domain(const std::string &domain_p) { _domain = domain_p; }

  inline std::string expires(void) const { return _expires; }

  inline void expires(int seconds_from_now = 0)
  {
    char buf[100];
    time_t now = time(0);
    now += seconds_from_now;
    struct tm tm = *gmtime(&now);
    strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S %Z", &tm);
    _expires = buf;
  }

  inline bool secure(void) const { return _secure; }

  inline void secure(bool secure_p) { _secure = secure_p; }

  inline bool httpOnly(void) const { return _http_only; }

  inline void httpOnly(bool http_only_p) { _http_only = http_only_p; }
};

/*!
 * For each request, the server creates a new instance of this class which is then passed
 * to the user supplied handler. It implements the basic features of the HTTP protocol
 * version 1.1. It is able to cope with chunked transfer and offers methods for reading and writing
 * from/to the http connection.
 *
 * An example of usage is as follows (within a request handler):
 *
 *     static void default_handler(Connection &conn, void *user_data)
 *     {
 *         conn.status(Connection::NOT_FOUND);
 *         conn.header("Content-Type", "text/text");
 *         conn.setBuffer();
 *         conn << "No handler available" << Connection::flush_data;
 *     }
 *
 */
class Connection
{
public:
  /*!
   * \typedef HttpMethods
   * Enumeration of the existing HTTP methods. Some rather exotic methods have been left out.
   */
  typedef enum {
    OPTIONS = 0,//!< Allows a client to determine the options and/or requirements associated with a resource
    GET = 1,//!< Retrieve whatever information (in the form of an entity) is identified by the Request-URI
    HEAD = 2,//!< Identical to GET except that the server MUST NOT return a message-body in the response.
    POST = 3,//!< Accept the entity enclosed in the request as a new subordinate of the resource identified by the
             //!< Request-URI
    PUT = 4,//!< Requests that the enclosed entity be stored under the supplied Request-URI
    DELETE = 5,//!< Requests that the origin server delete the resource identified by the Request-URI
    TRACE = 6,//!< Reflect the message received back to the client as the entity-body of a 200 (OK) response.
    CONNECT = 7,//!< For use with a proxy that can dynamically switch to being a tunnel
    OTHER = 8//!< Fallback....
  } HttpMethod;

  /*!
   * \typedef StatusCodes
   * Enumeration of HTTP status codes.
   */
  typedef enum {
    CONTINUE = 100,
    SWITCHING_PROTOCOLS = 101,
    PROCESSING = 102,

    OK = 200,
    CREATED = 201,
    ACCEPTED = 202,
    NONAUTHORITATIVE_INFORMATION = 203,
    NO_CONTENT = 204,
    RESET_CONTENT = 205,
    PARTIAL_CONTENT = 206,
    MULTI_STATUS = 207,
    ALREADY_REPORTED = 208,
    IM_USED = 226,

    MULTIPLE_CHOCIES = 300,
    MOVED_PERMANENTLY = 301,
    FOUND = 302,
    SEE_OTHER = 303,
    NOT_MODIFIED = 304,
    USE_PROXY = 305,
    SWITCH_PROXY = 306,
    TEMPORARY_REDIRECT = 307,
    PERMANENT_REDIRECT = 308,

    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    PAYMENT_REQUIRED = 402,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    METHOD_NOT_ALLOWED = 405,
    NOT_ACCEPTABLE = 406,
    PROXY_AUTHENTIFICATION_REQUIRED = 407,
    REQUEST_TIMEOUT = 408,
    CONFLICT = 409,
    GONE = 410,
    LENGTH_REQUIRED = 411,
    PRECONDITION_FAILED = 412,
    PAYLOAD_TOO_LARGE = 413,
    REQUEST_URI_TOO_LONG = 414,
    UNSUPPORTED_MEDIA_TYPE = 415,
    REQUEST_RANGE_NOT_SATISFIABLE = 416,
    EXPECTATION_FAILED = 417,
    I_AM_A_TEAPOT = 418,
    AUTHENTIFICATION_TIMEOUT = 419,
    METHOD_FAILURE = 420,
    TOO_MANY_REQUESTS = 429,
    UNAVAILABLE_FOR_LEGAL_REASONS = 451,

    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    BAD_GATEWAY = 502,
    SERVICE_UNAVAILABLE = 503,
    GATEWAY_TIMEOUT = 504,
    HTTP_VERSION_NOT_SUPPORTED = 505,
    UNKOWN_ERROR = 520
  } StatusCodes;

  /*!
   * \typedef Commands Used for flushing the output stream to the socket
   */
  typedef enum { flush_data } Commands;

  /*!
   * \typedef for uploaded files
   */
  typedef struct
  {
    std::string fieldname;//!< Name of the HTTP field
    std::string origname;//!< Original name of file
    std::string tmpname;//!< the temporary name of the file
    std::string mimetype;//!< The mimetype of the file
    size_t filesize;//!< The size of the file in bytes
  } UploadedFile;


private:
  Server *_server;//!< Pointer to the server class
  std::string _peer_ip;//!< IP number of client (peer)
  int _peer_port;//!< Port of peer/client
  std::string http_version;//!< Holds the HTTP version of the request
  bool _secure;//!< true if SSL used
  HttpMethod _method;//!< request method
  std::string _host;//!< host name that was used (for virtual hosts)
  std::string _uri;//!< uri of the request
  std::unordered_map<std::string, std::string> get_params;//!< parsed query string
  std::unordered_map<std::string, std::string> post_params;//!< parsed post parameters
  std::unordered_map<std::string, std::string> request_params;//!< parsed and merged get and post parameters
  std::unordered_map<std::string, std::string> header_in;//!< Input header fields
  std::unordered_map<std::string, std::string> header_out;//!< Output header fields
  std::unordered_map<std::string, std::string> _cookies;//!< Incoming cookies
  std::vector<UploadedFile> _uploads;//!< Upoaded files
  std::istream *ins;//!< incoming data stream
  std::ostream *os;//!< outgoing data stream
  std::string _tmpdir;//!< directory used to temporary storage of files (e.g. uploads)
  StatusCodes status_code;//!< Status code of response
  std::string status_string;//!< Short description of status code
  bool header_sent;//!< True if header already sent
  bool _keep_alive;//!< if true, don't close the socket after the request
  int _keep_alive_timeout;//!< timeout for connection
  bool _chunked_transfer_in;//!< Input data is chunked
  bool _chunked_transfer_out;//!< output data is sent in chunks
  bool _finished;//!< Transfer of response data finished
  char *_content;//!< Content if content-type is "text/plain", "application/json" etc.
  size_t content_length;//!< length of body in octets (used if not chunked transfer)
  std::string _content_type;//!< Content-type (mime type of content)
  std::ofstream *cachefile;//!< pointer to cache file
  char *outbuf;//!< If not NULL, pointer to the output buffer (buffered output used)
  size_t outbuf_size;//!< Actual size of output buffer
  size_t outbuf_inc;//!< Increment of outbuf buffer if it has to be enlarged
  size_t outbuf_nbytes;//!< number of bytes used so far in output buffer
  bool _reset_connection;//!< true, if connection should be reset (e.g. cors)

  /*!
   * Read, process and parse the HTTP request header
   */
  void process_header();

  /*
   * Add the given data to the output buffer. If necessary increase its size
   *
   * \param[in] buf Buffer containing the data
   * \param[in] n Number of bytes
   */
  void add_to_outbuf(char *buf, size_t n);

  /*!
   * Send the HTTP header. If n > 0, a "Content-Lenght" header is added
   *
   * \param[in] n Size of response body (0, if no body or chunked transfer is used)
   */
  void send_header(size_t n = 0);

  /*!
   * Finalize response and flush all buffers
   */
  void finalize();


public:
  /*!
   * Default constructor creates empty connection that cannot be used for anything
   */
  Connection(void);

  /*!
   * Constructor of a connection
   *
   * The constructor does most of the processing for a connection. In case of a PUT or POST
   * it reads the body of the message and prepares it for processing. The information can
   * be accessed by the postParams methods. In case of an upload of one or several files,
   * the information can be found using the uploads() method.
   *
   * \param[in] server_p Pointer to the server instance
   * \param[in] ins_p An input stream (constructed with \class StreamSock) of the HTTP socket
   * \param[in] os_p Output stream (constructed with \class StreamSock) of the HTTP socket
   * \param[in] Path to directory for temporaray files (e.g. uploads)
   * \param[in] If > 0, an output buffer is created.
   * \param[in] Increment size of output buffer (it will be increased by multiple of this size if necessary)
   */
  Connection(Server *server_p,
    std::istream *ins_p,
    std::ostream *os_p,
    const std::string &tmpdir_p,
    size_t buf_size = 0,
    size_t buf_inc = 8192);

  Connection(const Connection &conn);

  Connection &operator=(const Connection &other);

  /*!
   * Destructor which frees all resources
   */
  ~Connection();


  inline bool finished() { return _finished; }// TODO: Temporary for debugging!!

  /*!
   * Get the server
   */
  inline Server *server(void) { return _server; }

  /*!
   * Get the ip of the peer/client
   *
   * \returns String with peer IP (either IP4 or IP6)
   */
  inline std::string peer_ip(void) { return _peer_ip; }

  /*!
   * Set ip of peer
   *
   * \param ip String containing peer ip
   */
  inline void peer_ip(const std::string &ip) { _peer_ip = ip; }

  /*!
   * Get port number of peer
   *
   * \returns Port number of peer
   */
  inline int peer_port(void) { return _peer_port; }

  /*!
   * Set port of peer
   *
   * \param port Port number of peer
   */
  inline void peer_port(int port) { _peer_port = port; }


  /*!
   * Return true if a secure (SSL) connection is used
   */
  bool secure(void);

  /*!
   * Set the secure connection status
   */
  inline void secure(bool sec) { _secure = sec; }

  /*!
   * Get the request URI
   *
   * \returns std::string containig the hostname given
   */
  inline std::string host() { return _host; }

  /*!
   * Get the request URI
   *
   * \returns std::string containing the uri
   */
  inline std::string uri() { return _uri; }

  /*!
   * Returns the request method
   *
   * \returns Returns the methods as \typedef HttpMethod
   */
  inline HttpMethod method() { return _method; }

  /*!
   * Returns true if keep alive header was present in request
   *
   * \returns Keep alive status as boolean
   */
  inline bool keepAlive(void) { return _keep_alive; }

  inline void keepAlive(bool keep_alive_p) { _keep_alive = keep_alive_p; }

  /*!
   * Adds a keep-alive timeout to the socket
   *
   * \param[in] sock The input socket
   * \param[in] default_timeout Sets the default timeout of the
   * has not been a keep-alive header in the HTTP request.
   */
  int setupKeepAlive(int default_timeout = 20);

  /*!
   * Set the keep alive time (in seconds)
   *
   * \param[in] keep_alive_timeout Set the keep alive timeout to this value (in seconds)
   */
  inline void keepAliveTimeout(int keep_alive_timeout) { _keep_alive_timeout = keep_alive_timeout; }

  /*!
   * Return the keep alive timeout of the connection
   *
   * \returns keep alive timeout in seconds
   */
  inline int keepAliveTimeout(void) { return _keep_alive_timeout; }

  /*!
   * Sets the response status code
   *
   * \param[in] status_code_p Status code as defined in \typedef StatusCodes
   * \param[in] status_string_p Additional status code description that is added
   */
  void status(StatusCodes status_code_p, const std::string status_string_p = "");

  /*!
   * Returns a list of all header fields in the request as std::vector
   *
   * \returns List of all request header fields as std::vector
   */
  std::vector<std::string> header(void);

  /*!
   * Returns the value of the given header field. If the field does not
   * exist, an empty string is returned.
   *
   * \returns String with header field value
   */
  inline std::string &header(const std::string &name) { return header_in[name]; }

  /*
   * \brief Adds the given header field to the output header.
   *
   * This method is used to add header fields to the output header. This
   * is only possible as long as the header has not yet been sent. If buffered
   * output is used, the header and body are sent at the end of processing the request.
   * However, if an unbuffered connection is used, sending the first data of the message
   * body will send the header data. Afterwards no more header fields may be added!
   *
   * \param name Name of the header field
   * \param Value of the header field
   */
  void header(std::string name, std::string value);

  /*!
   * Send the CORS header with the given origin
   *
   * \params[in] origin URL of the origin
   */
  void corsHeader(const char *origin);

  /*!
   * Send the CORS header with the given origin
   *
   * \params[in] origin URL of the origin
   */
  void corsHeader(const std::string &origin);

  /*!
   * Returns a unordered_map of cookies
   */
  inline std::unordered_map<std::string, std::string> cookies(void) { return _cookies; };

  /*!
   * Set a cookie
   *
   * \param[in] cookie_p An instance of the cookie data
   */
  void cookies(const Cookie &cookie_p);

  /*!
   * Get the directory for temporary files
   *
   * \returns path of temporary directory
   */
  inline std::string tmpdir(void) { return _tmpdir; }

  /*!
   * setting the directory for temporary files
   *
   * \param[in] tmpdir_p String with the path to the directory to be used for temporary uploads
   */
  inline void tmpdir(const std::string &tmpdir_p) { _tmpdir = tmpdir_p; }

  /*!
   * Return a list of the get parameter names
   *
   * \returns List of get parameter names as std::vector
   */
  std::vector<std::string> getParams(void);

  /*!
   * Return the given get value. If the get parameter does not
   * exist, an empty string is returned.
   *
   * \param[in] name Name of the query parameter
   */
  std::string getParams(const std::string &name);

  /*!
   * Return a list of the post parameter names
   *
   * \returns List of post parameter names as std::vector
   */
  std::vector<std::string> postParams(void);

  /*!
   * Return the given post value. If the post parameter does not
   * exist, an empty string is returned.
   *
   * \param[in] name Name of the post parameter
   */
  std::string postParams(const std::string &name);

  /*!
   * Return the uploads
   *
   * \returns Vector of UploadedFile
   */
  inline std::vector<UploadedFile> uploads() { return _uploads; }

  /*!
   * Remove all files that have been uploaded from the temporary directory
   */
  bool cleanupUploads(void);


  /*!
   * Return a list of the request parameter names. Request parameters
   * are constructed by merging get and post parameters with get parameter
   * taking precedence over post parameters (thus overriding post parameters)
   *
   * \returns List of post parameter names as std::vector
   */
  std::vector<std::string> requestParams(void);

  /*!
   * Return the given request value. Request parameters
   * are constructed by merging get and post parameters with get parameter
   * taking precedence over post parameters (thus overriding post parameters)
   * If the request parameter does not
   * exist, an empty string is returned.
   *
   * \param[in] name Name of the post parameter
   */
  std::string requestParams(const std::string &name);

  /*!
   * returns a vector of components (separated by a ";") from a header value, e.g.
   * Content-Disposition: form-data; name="mycontrol"
   *
   * \param[in] valstr String to be parsed
   * \returns Vector of options
   */
  std::vector<std::string> process_header_value(const std::string &valstr);

  /*!
   * Returns the content length from PUT and DELETE requests
   *
   * \returns Content length
   */
  inline size_t contentLength(void) { return content_length; }

  /*!
   *  Get the content from PUT and DELETE requests
   *
   *  \returns Content as pointer to char
   */
  inline const char *content(void) const { return _content; }

  /*!
   * Get the content type for PUT and DELETE requests
   *
   * \returns Content type as string
   */
  inline std::string contentType(void) { return _content_type; }

  /*!
   * Add a Content-Length header. This method is used to add the size of the
   * response body if unbuffered IO is used. If a buffered connection is used,
   * the Content-Length header will be added automatically!
   *
   * \param[in] n Number of octets the body will have
   */
  inline void addContentLength(size_t n) { header("Content-Length", std::to_string(n)); }

  /*
   * Use a buffered connection. This method can be used a long as no body data has been added.
   *
   * \param[in] buf_size Initial buffer size
   * \param[in] buf_inc Increment size, if the buffer size has to be increased
   */
  void setBuffer(size_t buf_size = 8912, size_t buf_inc = 8912);

  inline bool isBuffered(void) { return (outbuf != nullptr); }

  /*!
   * Set the transfer mode for the response to chunked
   */
  void setChunkedTransfer();

  /*!
   * open a cache file
   *
   * \param[in] cfname Name of cache file
   */
  void openCacheFile(const std::string &cfname);

  /*!
   * close cache file
   */
  void closeCacheFile(void);

  /*!
   * test if a cachefile is open for writing...
   *
   * @return true, if cache file is open
   */
  inline bool isCacheFileOpen() { return (cachefile != nullptr); }

  /*!
   * Quasi "raw" data transmition. Sens the header if not yet done, and then
   * send the given data directly to the output without buffering etc.
   *
   * \param[in] buffer Data to be transfered
   * \param[in] n Number of bytes to be sent
   */
  void sendData(const void *buffer, size_t n);

  /*!
   * Send the given data. If the output is buffered, the data is added to the output
   * buffer. If the connection is unbuffered and chunked, a new chunk is sent containing the data.
   * If the connection is unbuffered but not chunked, the data is just sent directly.
   *
   * \param[in] buffer Data to be transfered
   * \param[in] n Number of bytes to be sent
   */
  void send(const void *buffer, size_t n);

  /*!
   * Send the given data. If the connection is buffered, add the data to the output buffer,
   * calculate the "Content-Lenght" header and add it, then send the whole response. After sending,
   * the connection is finished and no more data can be sent.
   * If the connection is unbuffered, just send a new chunk. Please note that the final empty
   * chunk will be sent of finalization of the connection (called by the destructor).
   *
   * \param[in] buffer Data to be transfered
   * \param[in] n Number of bytes to be sent
   */
  void sendAndFlush(const void *buffer, size_t n);

  /*!
   * Sends the data of a file to the connection. Supports HTTP range requests.
   *
   * \param[in] path Path to the file
   * \param[in] bufsize Buffer size for reading chunks (default: 8192 bytes)
   * \param[in] from Start byte position for range requests (0-based, inclusive, default: 0)
   * \param[in] to End byte position for range requests (0-based, inclusive, default: 0 = whole file)
   */
  void sendFile(const std::string &path, const size_t bufsize = 8192, const size_t from = 0, const size_t to = 0);

  /*!
   * Send the given string to the output. Uses \method send
   *
   * \param[in] str String to be sent
   *
   * \returns Connection instance
   */
  Connection &operator<<(const std::string &str);

  /*!
   * Send the given command (see \typedef Commands) to the output. Up to now
   * the only command availabe is Connection::flush_data
   *
   * \param[in] cmd Command to be sent.
   *
   * \returns Connection instance
   */
  Connection &operator<<(Commands cmd);

  /*!
   * Send a string representation of the given Error to the connection
   *
   * \param[in] err Error as might be thrown by the shttps package
   *
   * \returns Connection instance
   */
  Connection &operator<<(const Error &err);

  /*!
   * Sends the header and all data available.
   */
  void flush();

  /*!
   * Flags the connection to be reset
   */
  bool resetConnection() const { return _reset_connection; }
};

}// namespace shttps

#endif
