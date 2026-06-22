/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * The FFI-layer `ResponseSink` that drives the C `SipiResponse` callbacks.
 *
 * A configured Lua route emits its response through `server.print` /
 * `sendStatus` / `sendHeader` / `sendCookie`, which the `LuaServer` writes to the
 * abstract `shttps::ResponseSink` on its `RequestContext`. `FfiResponseSink`
 * forwards those onto the `SipiResponse` the Rust shell supplies — the FFI-layer
 * analogue of the transport's `ConnectionResponseSink`. It is the SAME response
 * seam the serve paths drive, never a parallel path: the route's body flows
 * through `resp.write` exactly as the image encoder's does.
 */
#ifndef SIPI_FFI_RESPONSE_SINK_H
#define SIPI_FFI_RESPONSE_SINK_H

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>

#include "ffi/sipi_ffi.h"// SipiResponse
#include "shttps/lua/request_context.h"// shttps::ResponseSink, ResponseCookie

namespace Sipi::ffi {

class FfiResponseSink : public shttps::ResponseSink
{
public:
  explicit FfiResponseSink(const SipiResponse &resp) : resp_(resp) {}

  void set_status(int status) override
  {
    if (resp_.set_status != nullptr) { resp_.set_status(resp_.ctx, status); }
  }

  void add_header(const std::string &name, const std::string &value) override
  {
    if (resp_.add_header != nullptr) { resp_.add_header(resp_.ctx, name.c_str(), value.c_str()); }
  }

  void add_cookie(const shttps::ResponseCookie &c) override
  {
    if (resp_.add_header != nullptr) { resp_.add_header(resp_.ctx, "Set-Cookie", render_cookie(c).c_str()); }
  }

  int write(const void *data, std::size_t len) override
  {
    if (resp_.write == nullptr) { return 0; }
    return resp_.write(resp_.ctx, static_cast<const std::uint8_t *>(data), len);
  }

  // set_buffer: framing is the transport's call (the Rust sink streams), so the
  // base-class no-op is correct.

private:
  // Render a Set-Cookie value with the field order Connection::cookies uses.
  static std::string render_cookie(const shttps::ResponseCookie &c)
  {
    std::string s = c.name + "=" + c.value;
    if (!c.path.empty()) { s += "; Path=" + c.path; }
    if (!c.domain.empty()) { s += "; Domain=" + c.domain; }
    if (c.expires_set) { s += "; Expires=" + http_date(c.expires_seconds); }
    if (c.secure) { s += "; Secure"; }
    if (c.http_only) { s += "; HttpOnly"; }
    return s;
  }

  // now + seconds as the Set-Cookie Expires date, matching Connection::Cookie
  // ("%a, %d %b %Y %H:%M:%S %Z"); gmtime_r for thread-safety behind the seam.
  static std::string http_date(int seconds_from_now)
  {
    std::time_t t = std::time(nullptr) + seconds_from_now;
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[100];
    std::strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S %Z", &tm);
    return buf;
  }

  const SipiResponse &resp_;
};

}// namespace Sipi::ffi

#endif// SIPI_FFI_RESPONSE_SINK_H
