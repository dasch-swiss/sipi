/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * The connection-less preflight builders behind `sipi_preflight` /
 * `sipi_file_preflight` (`ffi/sipi_ffi.h`).
 *
 * Same misuse-resistant shape as `serve_response.h`: a `build_*` step runs the
 * Lua hook and returns a pure value (`std::expected<PreflightDecision,
 * SipiStatus>`) — every fallible step (VM construction, Lua execution, result
 * validation) happens here, before anything is committed — and a single `apply`
 * drives the C output channel (the permission-type out-param + the key/value
 * callback). Both are wrapped by `sipi_guard` at the `extern "C"` entry.
 *
 * `PreflightDecision` is a clean value type (a permission enum + an open kv map)
 * so it ports 1:1 to the future Rust `PreFlight` trait return type: the C++
 * Lua hook is just one producer of it.
 */
#ifndef SIPI_FFI_PREFLIGHT_H
#define SIPI_FFI_PREFLIGHT_H

#include <expected>
#include <string>
#include <utility>
#include <vector>

#include "ffi/serve_response.h"// SipiStatus
#include "ffi/sipi_ffi.h"// SipiPermType, SipiKVFn
#include "shttps/lua/request_context.h"// shttps::RequestContext

namespace Sipi::ffi {

/*! The parsed result of a Lua preflight hook: the permission type plus the open
 *  key/value channel the hook returned (`infile` + any extras: `watermark`,
 *  `size`, `cookieUrl`, `tokenUrl`, `logoutUrl`, service pass-through). The
 *  caller compares the type and reads the kv it needs. `type` is the enum, so
 *  the kv never carries a `"type"` key. */
struct PreflightDecision
{
  SipiPermType type{};
  std::vector<std::pair<std::string, std::string>> kv;
};

/*! Run the IIIF `pre_flight(prefix, identifier, cookie)` Lua hook against the
 *  caller-supplied request context: build a per-call VM from the engine Lua
 *  config bound to `ctx` (so `server.header` / `server.cookies` / `server.get`
 *  resolve from the real request), run the hook, validate the result, return the
 *  decision. The `cookie` Lua argument is taken from `ctx` (the `cookie`
 *  header). `ctx` must outlive the call. Pure of the C output channel. Returns
 *  `SipiStatus::InternalError` (logged) on any Lua / validation failure, matching
 *  the legacy handler's 500. */
[[nodiscard]] std::expected<PreflightDecision, SipiStatus> build_preflight(const char *prefix,
  const char *identifier,
  shttps::RequestContext &ctx);

/*! Run the `file_pre_flight(filepath, cookie)` Lua hook — the `/file`
 *  media-serving path (audio / video / PDF / any non-IIIF file). Narrower valid
 *  permission set than `build_preflight` (allow / login / restrict / deny). */
[[nodiscard]] std::expected<PreflightDecision, SipiStatus> build_file_preflight(const char *filepath,
  shttps::RequestContext &ctx);

/*! The single place that drives the preflight C output channel: write the
 *  permission type, then emit each kv pair. */
void apply_preflight(PreflightDecision &&decision, SipiPermType *type, SipiKVFn emit_kv, void *kv_ctx);

/*! The Lua-facing permission string for a type (`SIPI_ALLOW` → `"allow"`, …).
 *  Used by the parity adapters in `SipiHttpServer.cpp`, on the current
 *  transport path, to rebuild the string-keyed `preflight_info` map the
 *  existing handlers consume. */
[[nodiscard]] const char *perm_type_to_string(SipiPermType type);

}// namespace Sipi::ffi

#endif// SIPI_FFI_PREFLIGHT_H
