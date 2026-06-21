/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * \brief Connection ↔ RequestContext parity glue (transport-only).
 *
 * Adapts a live `shttps::Connection` to the connection-less `RequestContext` /
 * `ResponseSink` seam the Lua runtime drives since the request_context
 * decoupling. The transport uses these to run the per-request Lua VM, and the
 * `SipiHttpServer` parity adapters use them to drive the Lua FFI entries
 * (`sipi_preflight`, `sipi_run_lua_route`) connection-less — both share this one
 * snapshot implementation so the FFI path sees exactly what the live VM sees.
 *
 * The whole file is deleted at the Phase C cutover, when the Rust shell builds
 * the `RequestContext` directly and the C++ transport goes away.
 */
#ifndef SHTTPS_TRANSPORT_CONNECTION_REQUEST_CONTEXT_H
#define SHTTPS_TRANSPORT_CONNECTION_REQUEST_CONTEXT_H

#include <cstddef>
#include <string>

#include "shttps/lua/request_context.h"
#include "shttps/transport/Connection.h"

namespace shttps {

/*!
 * A `ResponseSink` that forwards the Lua response back onto a live `Connection`.
 */
class ConnectionResponseSink : public ResponseSink
{
public:
  explicit ConnectionResponseSink(Connection &conn) : conn_(conn) {}

  void set_status(int status) override { conn_.status(static_cast<Connection::StatusCodes>(status)); }

  void add_header(const std::string &name, const std::string &value) override { conn_.header(name, value); }

  void add_cookie(const ResponseCookie &c) override
  {
    Cookie cookie(c.name, c.value);
    if (!c.path.empty()) { cookie.path(c.path); }
    if (!c.domain.empty()) { cookie.domain(c.domain); }
    if (c.expires_set) { cookie.expires(c.expires_seconds); }
    cookie.secure(c.secure);
    cookie.httpOnly(c.http_only);
    conn_.cookies(cookie);
  }

  int write(const void *data, std::size_t len) override
  {
    try {
      conn_.send(data, len);
      return 0;
    } catch (...) {
      return 1;
    }
  }

  void set_buffer(std::size_t buf_size, std::size_t buf_inc) override
  {
    if (buf_size > 0 && buf_inc > 0) {
      conn_.setBuffer(buf_size, buf_inc);
    } else if (buf_size > 0) {
      conn_.setBuffer(buf_size);
    } else {
      conn_.setBuffer();
    }
  }

private:
  Connection &conn_;
};

/*!
 * Snapshot a `Connection`'s request fields into a connection-less
 * `RequestContext` whose response points at `sink` (which must outlive the
 * returned context). Mirrors exactly what `LuaServer::createGlobals` reads.
 */
RequestContext make_request_context(Connection &conn, ResponseSink &sink);

}// namespace shttps

#endif// SHTTPS_TRANSPORT_CONNECTION_REQUEST_CONTEXT_H
