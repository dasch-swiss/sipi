/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/engine_context.h"

namespace Sipi::ffi {
namespace {
  // Process-wide engine state. Set once at startup (Phase B: by SipiHttpServer;
  // Phase C: by sipi_init) and only read thereafter, so no synchronization is
  // needed for the read path the serve functions take.
  EngineContext g_engine;
}// namespace

void set_engine_context(const EngineContext &ctx) { g_engine = ctx; }

const EngineContext &engine_context() { return g_engine; }

}// namespace Sipi::ffi
