/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/engine_context.h"

#include "logging/logger.h"
#include "util/Error.h"

namespace Sipi::ffi {
namespace {
  // Process-wide engine state. Installed once at startup and only read
  // thereafter, so no synchronization is needed for the read path the serve
  // functions take.
  EngineContext g_engine;
  bool g_engine_installed = false;
}// namespace

// Two callers install the context, one per surface: in production, `sipi_init`
// (`ffi/init.cpp`); in the retained oracle transport, `SipiHttpServer` at
// startup (its parity install). Both run once, before any serve call, and write
// the same shape — the oracle path exists only for the differential gate and
// dies with the C++ server. Documented here at the single sink rather than at
// each caller.
void set_engine_context(const EngineContext &ctx)
{
  g_engine = ctx;
  g_engine_installed = true;
}

const EngineContext &engine_context()
{
  // A missing install is a hard configuration error, not a silent all-disabled
  // serve: without it the pipeline would run with no cache/memory-budget and
  // no resolved image root. sipi_serve_image's sipi_guard turns this throw into
  // a clean 500 instead of undefined behaviour on an uninitialised engine.
  if (!g_engine_installed) {
    log_err("engine context not installed: sipi_init() must run before any serve call");
    throw shttps::Error("engine context not installed (sipi_init not called)");
  }
  return g_engine;
}

}// namespace Sipi::ffi
