/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * The engine-owned services + config the FFI image pipeline reads.
 *
 * `sipi_serve_image` is a C ABI of shape `(req, resp)` — it has no slot for the
 * cache, rate limiter, memory budget, or the server config knobs the decode
 * pipeline needs. `EngineContext` is that durable engine state, read by
 * `build_image_response`. In Phase C `sipi_init` constructs and installs it; in
 * the Phase B parity window the still-living `SipiHttpServer` installs
 * non-owning pointers to the services it already owns via `set_engine_context`.
 * The install call is the only throwaway part — the same shape as the
 * `Connection`→`SipiResponse` adapter that drives `sipi_serve_file` today — and
 * goes away with `SipiHttpServer` at the cutover.
 */
#ifndef SIPI_FFI_ENGINE_CONTEXT_H
#define SIPI_FFI_ENGINE_CONTEXT_H

#include <cstddef>
#include <string>

#include "SipiIO.h"// ScalingQuality (value member)

namespace Sipi {
class SipiCache;
class SipiRateLimiter;
class SipiMemoryBudget;
}// namespace Sipi

namespace Sipi::ffi {

/*! Engine services + config read by the IIIF image pipeline. The three service
 *  pointers are non-owning (the installer outlives every serve call) and may be
 *  null when the corresponding feature is disabled, mirroring the legacy
 *  `server->cache()/rate_limiter()/memory_budget()` accessors. */
struct EngineContext
{
  SipiCache *cache = nullptr;//!< file cache, or null when caching is off
  SipiRateLimiter *rate_limiter = nullptr;//!< per-client limiter, or null when off
  SipiMemoryBudget *memory_budget = nullptr;//!< decode memory budget, or null when off

  std::string imgroot;//!< image root: raw config value, for the Rust edge's path build (parity with imgroot())
  std::string resolved_imgroot;//!< realpath()-resolved image root, for the R2 containment check
  std::string docroot;//!< `/server` fileserver docroot: raw config value (the Rust edge canonicalises per request); empty = fileserver off
  std::string wwwroute;//!< URL prefix the docroot fileserver is mounted at (e.g. "/server"); empty = fileserver off
  bool prefix_as_path = true;//!< IIIF prefix is a path component under imgroot (config knob, exposed to the edge)
  int jpeg_quality = 60;//!< JPEG encode quality
  ScalingQuality scaling_quality{};//!< per-format scaling method
  std::size_t max_pixel_limit = 0;//!< max output pixels per request (0 = unlimited)
  int nthreads = 0;//!< configured worker-thread count; 0 = auto (the shell sizes its pool from host parallelism)
  std::size_t max_post_size = 0;//!< max POST body size in bytes (the Rust shell caps Lua-route uploads); 0 = unlimited
};

/*! Install the engine context (copied into a file-static). Called once at server
 *  startup in the Phase B parity path; superseded by `sipi_init` in Phase C. */
void set_engine_context(const EngineContext &ctx);

/*! The installed engine context. Throws `shttps::Error` if neither
 *  `set_engine_context` nor `sipi_init` has run — a missing install is a hard
 *  configuration error, caught by the serve entry's `sipi_guard` (→ 500), not a
 *  silent all-disabled serve. */
[[nodiscard]] const EngineContext &engine_context();

}// namespace Sipi::ffi

#endif// SIPI_FFI_ENGINE_CONTEXT_H
