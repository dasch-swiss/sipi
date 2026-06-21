/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/engine_context.h"

#include "logging/logger.h"
#include "shttps/util/Error.h"

namespace Sipi::ffi {
namespace {
  // Process-wide engine state. Set once at startup (Phase B: by SipiHttpServer;
  // Phase C: by sipi_init) and only read thereafter, so no synchronization is
  // needed for the read path the serve functions take.
  EngineContext g_engine;
  bool g_engine_installed = false;
}// namespace

void set_engine_context(const EngineContext &ctx)
{
  g_engine = ctx;
  g_engine_installed = true;
}

const EngineContext &engine_context()
{
  // A missing install is a hard configuration error, not a silent all-disabled
  // serve: without it the pipeline would run with no cache/rate-limit/budget and
  // no resolved image root. sipi_serve_image's sipi_guard turns this throw into
  // a clean 500 instead of undefined behaviour on an uninitialised engine.
  if (!g_engine_installed) {
    log_err("engine context not installed: sipi_init() must run before any serve call");
    throw shttps::Error("engine context not installed (sipi_init not called)");
  }
  return g_engine;
}

}// namespace Sipi::ffi
