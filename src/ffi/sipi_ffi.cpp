/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/sipi_ffi.h"

#include <cstdint>
#include <utility>

#include "ffi/engine_context.h"
#include "ffi/metrics_snapshot.h"
#include "ffi/preflight.h"
#include "ffi/serve_image.h"
#include "ffi/serve_response.h"
#include "observability/metrics.h"
#include "shttps/lua/request_context.h"// shttps::RequestContext (the opaque SipiRequestContext)

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

}// extern "C"
