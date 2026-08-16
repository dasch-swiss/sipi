/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * The engine-owned services + config the FFI image pipeline reads.
 *
 * `sipi_serve_image` is a C ABI of shape `(req, resp)` — it has no slot for the
 * cache, memory budget, or the server config knobs the decode
 * pipeline needs. `EngineContext` is that durable engine state, read by
 * `build_image_response`. `sipi_init` constructs and installs it once at startup
 * via `set_engine_context`, holding non-owning pointers to the services it owns.
 */
#ifndef SIPI_FFI_ENGINE_CONTEXT_H
#define SIPI_FFI_ENGINE_CONTEXT_H

#include <cstddef>
#include <string>

#include "SipiIO.h"// ScalingQuality (value member)

namespace Sipi {
class SipiCache;
class SipiMemoryBudget;
}// namespace Sipi

namespace Sipi::ffi {

/*! Engine services + config read by the IIIF image pipeline. The two service
 *  pointers are non-owning (the installer outlives every serve call) and may be
 *  null when the corresponding feature is disabled. */
struct EngineContext
{
  SipiCache *cache = nullptr;//!< file cache, or null when caching is off
  SipiMemoryBudget *memory_budget = nullptr;//!< full-lane decode memory budget (always installed; monitor or enforce)
  //!< A decode whose estimated peak memory is >= this threshold is a full-lane
  //!< decode and is charged against `memory_budget`; below it is a tile decode
  //!< and bypasses the budget. Single-sourced in the shell config and passed
  //!< over the seam at init (DUNE-003), so the two classifiers cannot drift.
  std::size_t large_decode_threshold_bytes = 0;

  std::string imgroot;//!< image root: raw config value, for the Rust edge's path build
  std::string resolved_imgroot;//!< realpath()-resolved image root, for the R2 containment check
  std::string docroot;//!< `/server` fileserver docroot: raw config value (the Rust edge canonicalises per request); empty = fileserver off
  std::string wwwroute;//!< URL prefix the docroot fileserver is mounted at (e.g. "/server"); empty = fileserver off
  bool prefix_as_path = true;//!< IIIF prefix is a path component under imgroot (config knob, exposed to the edge)
  int jpeg_quality = 60;//!< JPEG encode quality
  ScalingQuality scaling_quality{};//!< per-format scaling method
  std::size_t max_pixel_limit = 0;//!< max output pixels per request (0 = unlimited)
  int nthreads = 0;//!< configured worker-thread count; 0 = auto (the shell sizes its pool from host parallelism)
  int port = 3333;//!< configured HTTP listen port (the Lua config `sipi.port`); a fallback for the Rust edge's listener bind when no `--serverport`/`SIPI_SERVERPORT`/`SIPI_RS_PORT` selected one
  std::size_t max_post_size = 0;//!< max POST body size in bytes (the Rust shell caps Lua-route uploads); 0 = unlimited
};

/*! Install the engine context (copied into a file-static). Called once at
 *  startup by `sipi_init`. */
void set_engine_context(const EngineContext &ctx);

/*! The installed engine context. Throws `shttps::Error` if neither
 *  `set_engine_context` nor `sipi_init` has run — a missing install is a hard
 *  configuration error, caught by the serve entry's `sipi_guard` (→ 500), not a
 *  silent all-disabled serve. */
[[nodiscard]] const EngineContext &engine_context();

}// namespace Sipi::ffi

#endif// SIPI_FFI_ENGINE_CONTEXT_H
