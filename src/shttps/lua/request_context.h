/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * \brief Connection-less request/response view for the Lua runtime.
 *
 * `RequestContext` is the buffered, transport-free view of an HTTP request and
 * its response sink that drives the C++ `LuaServer` without an
 * `shttps::Connection`. The caller populates the request fields, points
 * `response` at a concrete `ResponseSink`, and runs the Lua VM; the `server.*`
 * bindings read the request fields and write through the sink.
 *
 * The `ResponseSink` is the in-layer adapter to the single response seam (the
 * Phase A `OutputSink` pattern): the FFI seam supplies a `SipiResponse`-backed
 * implementation, the transport supplies a `Connection`-backed one for the
 * parity path. The Lua module therefore names no transport or FFI type, which
 * is what lets `lua/` be built below `//src/ffi` without depending on
 * `transport/`.
 *
 * Kept deliberately thin — the whole Lua runtime is reimplemented in mlua in a
 * later strangler slice.
 */
#ifndef SHTTPS_LUA_REQUEST_CONTEXT_H
#define SHTTPS_LUA_REQUEST_CONTEXT_H

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace shttps {

/*!
 * HTTP method understood by the Lua route table. Lua-module-local so the module
 * carries no transport (`Connection`) type; the transport maps its own
 * `Connection::HttpMethod` onto this at the boundary.
 */
enum class HttpMethod { OPTIONS, GET, HEAD, POST, PUT, DELETE, TRACE, CONNECT, OTHER };

/*!
 * An uploaded file as seen by Lua (`server.uploads`, `server.copyTmpfile`),
 * decoupled from `shttps::Connection::UploadedFile`.
 */
struct UploadedFile
{
  std::string fieldname;//!< Name of the HTTP field
  std::string origname;//!< Original name of the file
  std::string tmpname;//!< Temporary on-disk name
  std::string mimetype;//!< MIME type of the file
  std::size_t filesize{};//!< Size of the file in bytes
};

/*!
 * A cookie set by Lua (`server.sendCookie`), decoupled from `shttps::Cookie`.
 * The concrete `ResponseSink` renders it to a `Set-Cookie` header. Defaults
 * mirror `shttps::Cookie` (secure by default, not http-only).
 */
struct ResponseCookie
{
  std::string name;
  std::string value;
  std::string path;//!< empty = unset
  std::string domain;//!< empty = unset
  int expires_seconds{};//!< seconds from now; only honoured when `expires_set`
  bool expires_set = false;
  bool secure = true;
  bool http_only = false;
};

/*!
 * Abstract response sink: where the Lua `server.sendStatus` / `sendHeader` /
 * `sendCookie` / `print` / `setBuffer` bindings and the SIPI `img:write`
 * binding deliver the response. The single concrete sink for the transport
 * parity path forwards to `shttps::Connection`; the FFI seam supplies one that
 * forwards to the C `SipiResponse` callbacks.
 */
struct ResponseSink
{
  virtual ~ResponseSink() = default;

  virtual void set_status(int status) = 0;
  virtual void add_header(const std::string &name, const std::string &value) = 0;
  virtual void add_cookie(const ResponseCookie &cookie) = 0;

  /*!
   * Body bytes (`server.print` and the `img:write` codec callback). Returns 0
   * on success, mirroring the `SipiWriteFn` C-ABI contract.
   */
  virtual int write(const void *data, std::size_t len) = 0;

  /*!
   * Output-buffering hint (`server.setBuffer`). A size of 0 means "sink
   * default". Defaults to a no-op — body framing is the transport's call.
   */
  virtual void set_buffer(std::size_t buf_size, std::size_t buf_inc) { (void)buf_size, (void)buf_inc; }
};

/*!
 * Buffered, connection-less view of an HTTP request and its response sink.
 */
struct RequestContext
{
  // --- request inputs (populated by the caller before running Lua) ---
  HttpMethod method = HttpMethod::OTHER;
  std::string client_ip;
  int client_port = 0;
  bool secure = false;
  std::string host;
  std::string uri;
  std::unordered_map<std::string, std::string> headers;//!< name -> value (server.header, requireAuth)
  std::unordered_map<std::string, std::string> cookies;//!< incoming cookies
  std::vector<std::pair<std::string, std::string>> get_params;
  std::vector<std::pair<std::string, std::string>> post_params;
  std::vector<std::pair<std::string, std::string>> request_params;
  std::vector<UploadedFile> uploads;
  std::string content;//!< request body (POST)
  std::string content_type;
  std::string jwt_secret;//!< secret for server.generate_jwt / server.decode_jwt

  // --- response sink (caller-owned; must outlive the Lua run) ---
  ResponseSink *response = nullptr;

  /*!
   * `server.shutdown()` hook. The transport path stops the server; the FFI path
   * may leave it empty (unsupported there).
   */
  std::function<void()> shutdown_hook;
};

}// namespace shttps

#endif
