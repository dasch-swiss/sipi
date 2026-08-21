/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/sipi_ffi.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "SipiImage.h"// SipiImage::read_shape (sipi_image_dims)
#include "ffi/engine_context.h"
#include "ffi/metrics_snapshot.h"
#include "ffi/serve_image.h"
#include "ffi/serve_response.h"
#include "ffi/serve_timings.h"// serve_timings_reset/export (sipi_serve_timings_take)
#include "generated/SipiVersion.h"// VERSION / BUILD_SCM_REVISION (sipi_build_version/commit)
#include "logging/logger.h"// set_log_trace_context (sipi_set_log_trace_context)
#include "observability/metrics.h"
#include "util/Parsing.h"// shttps::Parsing::getBestFileMimetype (sipi_mimetype)

namespace {


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
    // Reset the per-phase timing accumulator for this thread; the phase timers in
    // build_image_response + the streamed encode fill it, and the shell reads it
    // back via sipi_serve_timings_take right after this returns.
    Sipi::ffi::serve_timings_reset();
    const auto cancelled = [resp] { return resp->cancelled != nullptr && resp->cancelled(resp->ctx) != 0; };
    auto result = Sipi::ffi::build_image_response(*req, Sipi::ffi::engine_context(), cancelled);
    if (!result) { return static_cast<int>(result.error()); }
    Sipi::ffi::apply(std::move(*result), *resp);
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

void sipi_serve_timings_take(SipiServeTimings *out) { Sipi::ffi::serve_timings_export(out); }

int sipi_phase_count(void) { return SIPI_PHASE_COUNT; }

const char *sipi_build_version(void) { return VERSION; }

const char *sipi_build_commit(void) { return BUILD_SCM_REVISION; }

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

int sipi_docroot(const char **out)
{
  // Guard-only edge probe — a pure read of the installed engine context, like
  // sipi_imgroot. The raw config value (the Rust edge canonicalises per request);
  // empty when no fileserver is configured. Points at the process-static
  // EngineContext copy, valid for the process lifetime.
  return Sipi::ffi::sipi_guard([&] {
    *out = Sipi::ffi::engine_context().docroot.c_str();
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_wwwroute(const char **out)
{
  return Sipi::ffi::sipi_guard([&] {
    *out = Sipi::ffi::engine_context().wwwroute.c_str();
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

int sipi_max_post_size(size_t *out)
{
  // Guard-only edge probe — a pure read of the installed engine context, like
  // sipi_port. 0 means the config left POST size unlimited.
  return Sipi::ffi::sipi_guard([&] {
    *out = Sipi::ffi::engine_context().max_post_size;
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_admission_mode(const char **out)
{
  // Guard-only edge probe — the resolved admission mode ("basic"|"advanced"),
  // read back so the shell's two-lane pool runs the same mode as the engine's
  // memory budget (single authority = the engine's resolved config). Points at
  // the process-static EngineContext copy, valid for the process lifetime.
  return Sipi::ffi::sipi_guard([&] {
    *out = Sipi::ffi::engine_context().admission_mode.c_str();
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_tiles_memory_ratio(double *out)
{
  // Guard-only edge probe — the tile reserve fraction, read back for the shell's
  // config-fingerprint metric.
  return Sipi::ffi::sipi_guard([&] {
    *out = Sipi::ffi::engine_context().tiles_memory_ratio;
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_large_decode_threshold_bytes(size_t *out)
{
  // Guard-only edge probe — the tile/full classifier threshold, read back so the
  // shell classifies against the same value the engine charges the budget by.
  return Sipi::ffi::sipi_guard([&] {
    *out = Sipi::ffi::engine_context().large_decode_threshold_bytes;
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_memory_limit_bytes(size_t *out)
{
  // Guard-only edge probe — the resolved RAM envelope (post 0→auto-detect), read
  // back for the shell's config-fingerprint metric.
  return Sipi::ffi::sipi_guard([&] {
    *out = Sipi::ffi::engine_context().memory_limit_bytes;
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

int sipi_image_dims(const char *resolved_path, SipiImageDims *out, SipiEssentialsFn emit, void *ctx)
{
  // Header-only shape read. The Rust edge owns existence + containment (R1/R2)
  // before calling, so read_shape throwing here is a genuine engine failure →
  // 500 via the guard (read_shape never returns FAILURE; it throws). Native
  // shape only: numpages and tile_* (with width/height) drive info.json
  // sizes[]/tiles[]; the pyramid is derived from the tile grid.
  // One read_shape() call also carries the Essentials identity (origmimetype/
  // origname) when the file has one — emitted through the optional `emit`
  // callback (NULL when the caller, e.g. info.json, doesn't need it), so a
  // caller that wants both the shape and the identity pays for a single read.
  return Sipi::ffi::sipi_guard([&] {
    const Sipi::SipiImage probe;
    const Sipi::SipiImgInfo info = probe.read_shape(resolved_path);
    out->width = static_cast<std::uint32_t>(info.width);
    out->height = static_cast<std::uint32_t>(info.height);
    out->numpages = static_cast<std::uint32_t>(info.numpages);
    out->tile_width = static_cast<std::uint32_t>(info.tile_width);
    out->tile_height = static_cast<std::uint32_t>(info.tile_height);
    if (emit != nullptr && info.success == Sipi::SipiImgInfo::ALL) {
      emit(ctx, info.origmimetype.c_str(), info.origname.c_str());
    }
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

    // The singleton stores counters as uint64 and gauges as int64 (a gauge may
    // be negative: the cache size limit is -1 when unlimited). These readers make
    // the snapshot's integral narrowing explicit at the seam.
    const auto counter = [](const Sipi::observability::Counter &c) { return c.Value(); };
    const auto gauge = [](const Sipi::observability::Gauge &g) { return g.Value(); };

    out->cache_hits_total = counter(m.cache_hits_total);
    out->cache_misses_total = counter(m.cache_misses_total);
    out->cache_evictions_total = counter(m.cache_evictions_total);
    out->cache_skips_total = counter(m.cache_skips_total);
    out->client_disconnected_total = counter(m.client_disconnected_total);
    out->memory_alloc_failures_total = counter(m.memory_alloc_failures_total);
    out->rejected_connections_total = counter(m.rejected_connections_total);

    out->decode_memory_acquired_total = counter(m.decode_memory_acquired);
    out->decode_memory_rejected_total = counter(m.decode_memory_rejected);
    out->decode_memory_shadow_rejected_total = counter(m.decode_memory_shadow_rejected);
    out->decode_memory_near_limit_total = counter(m.decode_memory_near_limit_total);

    out->tiff_pyramid_reduced_decodes_total = counter(m.tiff_pyramid_reduced_decodes_total);

    out->decode_memory_too_large_total = counter(m.decode_memory_too_large_total);
    out->decode_memory_shadow_too_large_total = counter(m.decode_memory_shadow_too_large_total);

    out->waiting_connections = gauge(m.waiting_connections);
    out->cache_size_bytes = gauge(m.cache_size_bytes);
    out->cache_files = gauge(m.cache_files);
    out->cache_size_limit_bytes = gauge(m.cache_size_limit_bytes);
    out->cache_files_limit = gauge(m.cache_files_limit);
    out->decode_memory_budget_bytes = gauge(m.decode_memory_budget_bytes);
    out->decode_memory_used_bytes = gauge(m.decode_memory_used_bytes);

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

void sipi_set_outbound_traceparent(const char *traceparent)
{
  // Void + cannot meaningfully fail; swallow any allocation failure so no C++
  // exception crosses the boundary (the boundary contract is uniform).
  try {
    ::set_outbound_traceparent(traceparent);
  } catch (...) {
  }
}

}// extern "C"
