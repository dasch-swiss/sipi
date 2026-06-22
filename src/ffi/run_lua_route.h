/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * Run a configured Lua route connection-less behind the seam — the engine-side
 * replacement for the transport's `script_handler` (deleted at the cutover, when
 * the Rust shell owns route dispatch).
 *
 * Builds a per-request `LuaServer` from the engine-held config VM bound to the
 * caller's request context, reads the route's script, and executes it; the
 * route's `server.print` / `sendStatus` / `sendHeader` / `sendCookie` output
 * flows through an `FfiResponseSink` onto the C `SipiResponse`. Pure `.lua`
 * scripts run as one chunk; embedded `.elua` alternates raw HTML and `<lua>…
 * </lua>` chunks. Wrapped by `sipi_run_lua_route` under `sipi_guard`.
 */
#ifndef SIPI_FFI_RUN_LUA_ROUTE_H
#define SIPI_FFI_RUN_LUA_ROUTE_H

#include "ffi/sipi_ffi.h"// SipiResponse
#include "shttps/lua/request_context.h"// shttps::RequestContext

namespace Sipi::ffi {

/*! Execute the route at `script_path` against `rc`, emitting its response through
 *  `resp`. Sets `rc.response` to an `FfiResponseSink` over `resp` for the call.
 *  Returns 0 once the response is emitted, 404 when the script is not readable,
 *  or 500 on a Lua error / unknown script extension. A 404/500 returned before
 *  any byte is written lets the caller render a bare error; once the body has
 *  started, the status is already committed and the stream simply truncates
 *  (matching the transport). */
int run_lua_route(const char *script_path, shttps::RequestContext &rc, const SipiResponse &resp);

}// namespace Sipi::ffi

#endif// SIPI_FFI_RUN_LUA_ROUTE_H
