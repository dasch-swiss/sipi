/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/sipi_ffi.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "SipiImage.h"// SipiImage::read_shape (sipi_image_dims)
#include "ffi/engine_context.h"
#include "ffi/lua_config.h"// make_lua_server (sipi_has_preflight)
#include "ffi/metrics_snapshot.h"
#include "ffi/preflight.h"
#include "ffi/serve_image.h"
#include "ffi/serve_response.h"
#include "logging/logger.h"// set_log_trace_context (sipi_set_log_trace_context)
#include "observability/metrics.h"
#include "shttps/lua/request_context.h"// shttps::RequestContext (the opaque SipiRequestContext)
#include "shttps/util/Parsing.h"// shttps::Parsing::getBestFileMimetype (sipi_mimetype)

namespace {

// Map an HTTP method string to the Lua-facing enum (request_context.h), matching
// the transport's Connection::method() → HttpMethod mapping. Unknown → OTHER.
shttps::HttpMethod parse_http_method(const char *method)
{
  const std::string m = (method != nullptr) ? method : "";
  if (m == "GET") { return shttps::HttpMethod::GET; }
  if (m == "HEAD") { return shttps::HttpMethod::HEAD; }
  if (m == "POST") { return shttps::HttpMethod::POST; }
  if (m == "PUT") { return shttps::HttpMethod::PUT; }
  if (m == "DELETE") { return shttps::HttpMethod::DELETE; }
  if (m == "OPTIONS") { return shttps::HttpMethod::OPTIONS; }
  if (m == "TRACE") { return shttps::HttpMethod::TRACE; }
  if (m == "CONNECT") { return shttps::HttpMethod::CONNECT; }
  return shttps::HttpMethod::OTHER;
}

// Lower-case a header name, matching make_request_context (the Lua hooks look
// headers up by their lower-cased name, e.g. ctx.headers["cookie"]).
std::string ascii_lower(const char *s)
{
  std::string r = (s != nullptr) ? s : "";
  std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return r;
}

const char *nz(const char *s) { return (s != nullptr) ? s : ""; }

}// namespace

extern "C" {

int sipi_serve_file(const char *resolved_path, const char *range, const SipiResponse *resp)
{
  // The whole body is wrapped so no C++ exception escapes into the caller (UB
  // across extern "C"). build_file_response runs every fallible step before the
  // response is committed, so a failure is rendered as a clean status code by
  // the caller; apply is the only code that touches the response callbacks.
  return Sipi::ffi::sipi_guard([&] {
    auto result = Sipi::ffi::build_file_response(resolved_path, range);
    if (!result) { return static_cast<int>(result.error()); }
    Sipi::ffi::apply(std::move(*result), *resp);
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_serve_image(const SipiServeRequest *req, const SipiResponse *resp)
{
  // Same shape as sipi_serve_file: build (pure, every fallible step pre-commit)
  // → apply (the only place that touches the response callbacks), all under the
  // no-throw guard. A return of 499 (SipiStatus::ClientGone) means the client
  // vanished mid-decode and nothing was emitted — the caller renders no error.
  return Sipi::ffi::sipi_guard([&] {
    const auto cancelled = [resp] { return resp->cancelled != nullptr && resp->cancelled(resp->ctx) != 0; };
    auto result = Sipi::ffi::build_image_response(*req, Sipi::ffi::engine_context(), cancelled);
    if (!result) { return static_cast<int>(result.error()); }
    Sipi::ffi::apply(std::move(*result), *resp);
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_preflight(const char *prefix,
  const char *identifier,
  SipiRequestContext *ctx,
  SipiPermType *type,
  SipiKVFn emit_kv,
  void *kv_ctx)
{
  // Same build/apply/guard shape as the serve entries: build_preflight runs the
  // Lua hook against the caller's request context and every fallible step (VM
  // build, execution, result validation) before anything is committed;
  // apply_preflight is the only code that touches the C output channel (the type
  // out-param + the kv callback); all under the no-throw guard. The opaque
  // SipiRequestContext is the C++ shttps::RequestContext the caller built.
  return Sipi::ffi::sipi_guard([&] {
    auto &rc = *reinterpret_cast<shttps::RequestContext *>(ctx);
    auto result = Sipi::ffi::build_preflight(prefix, identifier, rc);
    if (!result) { return static_cast<int>(result.error()); }
    Sipi::ffi::apply_preflight(std::move(*result), type, emit_kv, kv_ctx);
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_file_preflight(const char *filepath,
  SipiRequestContext *ctx,
  SipiPermType *type,
  SipiKVFn emit_kv,
  void *kv_ctx)
{
  // The /file media-serving preflight; identical shape to sipi_preflight, runs
  // the file_pre_flight hook over a resolved filepath.
  return Sipi::ffi::sipi_guard([&] {
    auto &rc = *reinterpret_cast<shttps::RequestContext *>(ctx);
    auto result = Sipi::ffi::build_file_preflight(filepath, rc);
    if (!result) { return static_cast<int>(result.error()); }
    Sipi::ffi::apply_preflight(std::move(*result), type, emit_kv, kv_ctx);
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_metrics_snapshot(SipiMetricsSnapshot *out)
{
  // A thin read of the engine metrics singleton — no response sink and no
  // fallible pre-commit work, so it needs no build/apply split (that shape
  // exists to drive the response callbacks correctly). Only the no-throw guard
  // applies: the boundary contract is uniform — no entry lets a C++ exception
  // cross into Rust. `out` is a caller-owned buffer, trusted like the response
  // sinks of the serve entries.
  return Sipi::ffi::sipi_guard([&] {
    auto &m = Sipi::observability::Metrics::instance();

    // prometheus stores every value as a double; the snapshot narrows to the
    // integral type the OTel instrument wants — uint64 for monotonic counters,
    // int64 for gauges (which may be negative: the cache size limit is -1 when
    // unlimited).
    const auto counter = [](const prometheus::Counter &c) { return static_cast<std::uint64_t>(c.Value()); };
    const auto gauge = [](const prometheus::Gauge &g) { return static_cast<std::int64_t>(g.Value()); };

    out->cache_hits_total = counter(m.cache_hits_total);
    out->cache_misses_total = counter(m.cache_misses_total);
    out->cache_evictions_total = counter(m.cache_evictions_total);
    out->cache_skips_total = counter(m.cache_skips_total);
    out->image_too_large_total = counter(m.image_too_large_total);
    out->client_disconnected_total = counter(m.client_disconnected_total);
    out->memory_alloc_failures_total = counter(m.memory_alloc_failures_total);
    out->rejected_connections_total = counter(m.rejected_connections_total);

    out->rate_limit_allowed_total = counter(m.rate_limit_allowed);
    out->rate_limit_rejected_total = counter(m.rate_limit_rejected);
    out->rate_limit_shadow_rejected_total = counter(m.rate_limit_shadow_rejected);
    out->rate_limit_near_limit_total = counter(m.rate_limit_near_limit_total);

    out->decode_memory_acquired_total = counter(m.decode_memory_acquired);
    out->decode_memory_rejected_total = counter(m.decode_memory_rejected);
    out->decode_memory_shadow_rejected_total = counter(m.decode_memory_shadow_rejected);
    out->decode_memory_near_limit_total = counter(m.decode_memory_near_limit_total);

    out->waiting_connections = gauge(m.waiting_connections);
    out->cache_size_bytes = gauge(m.cache_size_bytes);
    out->cache_files = gauge(m.cache_files);
    out->cache_size_limit_bytes = gauge(m.cache_size_limit_bytes);
    out->cache_files_limit = gauge(m.cache_files_limit);
    out->rate_limit_clients_tracked = gauge(m.rate_limit_clients_tracked);
    out->decode_memory_budget_bytes = gauge(m.decode_memory_budget_bytes);
    out->decode_memory_used_bytes = gauge(m.decode_memory_used_bytes);

    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_imgroot(int resolved, const char **out)
{
  // A pure read of the installed engine context — guard-only (no response sink,
  // no fallible pre-commit work). engine_context() throws if sipi_init has not
  // run; the guard turns that into a 500. The returned pointer is into the
  // process-static EngineContext copy, valid for the process lifetime.
  return Sipi::ffi::sipi_guard([&] {
    const auto &eng = Sipi::ffi::engine_context();
    *out = (resolved != 0 ? eng.resolved_imgroot : eng.imgroot).c_str();
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_prefix_as_path(int *out)
{
  return Sipi::ffi::sipi_guard([&] {
    *out = Sipi::ffi::engine_context().prefix_as_path ? 1 : 0;
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_nthreads(int *out)
{
  // Guard-only edge probe — a pure read of the installed engine context, like
  // sipi_prefix_as_path. 0 means the config left nthreads auto; the Rust shell
  // resolves that against the host parallelism, so the C++ server's cores-1
  // auto-detect formula stays out of the seam.
  return Sipi::ffi::sipi_guard([&] {
    *out = Sipi::ffi::engine_context().nthreads;
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_image_dims(const char *resolved_path, SipiImageDims *out)
{
  // Header-only shape read. The Rust edge owns existence + containment (R1/R2)
  // before calling, so read_shape throwing here is a genuine engine failure →
  // 500 via the guard (read_shape never returns FAILURE; it throws). Native
  // shape only: numpages/tile_*/clevels drive info.json sizes[]/tiles[].
  return Sipi::ffi::sipi_guard([&] {
    const Sipi::SipiImage probe;
    const Sipi::SipiImgInfo info = probe.read_shape(resolved_path);
    out->width = static_cast<std::uint32_t>(info.width);
    out->height = static_cast<std::uint32_t>(info.height);
    out->numpages = static_cast<std::uint32_t>(info.numpages);
    out->tile_width = static_cast<std::uint32_t>(info.tile_width);
    out->tile_height = static_cast<std::uint32_t>(info.tile_height);
    out->clevels = static_cast<std::uint32_t>(info.clevels);
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_mimetype(const char *resolved_path, SipiStrFn emit, void *ctx)
{
  // One source of truth for MIME mapping: the same libmagic-backed sniff the
  // /file and info.json paths use. Emitted via a callback so the seam returns no
  // owned C string. The Rust edge owns existence; a libmagic failure throws →
  // 500 via the guard.
  return Sipi::ffi::sipi_guard([&] {
    const std::string mime = shttps::Parsing::getBestFileMimetype(resolved_path);
    emit(ctx, mime.c_str());
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

SipiRequestContext *sipi_make_request_context(const char *method,
  const char *client_ip,
  int client_port,
  int secure,
  const char *host,
  const char *uri,
  const SipiStrPair *headers,
  size_t n_headers,
  const SipiStrPair *cookies,
  size_t n_cookies)
{
  // Returns a pointer, so it can't use sipi_guard (which returns int); a throw
  // collapses to NULL. The heap RequestContext is owned by the Rust caller and
  // released via sipi_free_request_context. Fields preflight does not read
  // (get/post params, uploads, body) stay default-empty (YAGNI); jwt_secret +
  // response sink are filled by make_lua_server / left null (preflight read-only).
  try {
    auto ctx = std::make_unique<shttps::RequestContext>();
    ctx->method = parse_http_method(method);
    ctx->client_ip = nz(client_ip);
    ctx->client_port = client_port;
    ctx->secure = secure != 0;
    ctx->host = nz(host);
    ctx->uri = nz(uri);
    for (size_t i = 0; i < n_headers; ++i) { ctx->headers[ascii_lower(headers[i].name)] = nz(headers[i].value); }
    for (size_t i = 0; i < n_cookies; ++i) { ctx->cookies[nz(cookies[i].name)] = nz(cookies[i].value); }
    return reinterpret_cast<SipiRequestContext *>(ctx.release());
  } catch (...) {
    return nullptr;
  }
}

void sipi_free_request_context(SipiRequestContext *ctx)
{
  delete reinterpret_cast<shttps::RequestContext *>(ctx);
}

int sipi_has_preflight(int *out)
{
  // Build a VM from the engine Lua config (which runs the init script, defining
  // the hooks) and check whether pre_flight is defined — the Rust analog of the
  // C++ luaserver.luaFunctionExists gate. Builds a VM, so the shell calls this
  // once at startup. An empty context suffices (we only inspect the globals).
  return Sipi::ffi::sipi_guard([&] {
    shttps::RequestContext ctx;
    auto vm = Sipi::ffi::make_lua_server(ctx);
    *out = vm->luaFunctionExists("pre_flight") ? 1 : 0;
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_has_file_preflight(int *out)
{
  return Sipi::ffi::sipi_guard([&] {
    shttps::RequestContext ctx;
    auto vm = Sipi::ffi::make_lua_server(ctx);
    *out = vm->luaFunctionExists("file_pre_flight") ? 1 : 0;
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

void sipi_set_log_trace_context(const char *trace_id, const char *span_id)
{
  // Void + cannot meaningfully fail; swallow any allocation failure so no C++
  // exception crosses the boundary (the boundary contract is uniform).
  try {
    ::set_log_trace_context(trace_id, span_id);
  } catch (...) {
  }
}

}// extern "C"
