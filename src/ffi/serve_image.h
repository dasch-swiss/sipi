/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * The IIIF image pipeline behind the FFI seam (strangler-fig Phase B).
 *
 * `decide_serve_image` is the transport-pure decision for an IIIF image
 * request, the `serve_iiif` decode path carved out of `SipiHttpServer`. It
 * reconstructs the typed IIIF params from the flat `SipiServeRequest`, runs
 * admission (rate-limit / cache / memory-budget), builds the canonical URL, and
 * then decodes + transforms — every fallible step *before* the response is
 * committed, so a failure is a clean status code. It returns either a
 * `FileBody` (cache hit or direct passthrough → `sendFile`) or a `StreamBody`
 * whose producer runs only the encode (the rarely-failing tail), teeing to the
 * cache file with the DEV-6660 integrity guard.
 */
#ifndef SIPI_FFI_SERVE_IMAGE_H
#define SIPI_FFI_SERVE_IMAGE_H

#include <expected>
#include <functional>

#include "ffi/engine_context.h"
#include "ffi/serve_response.h"
#include "ffi/sipi_ffi.h"

namespace Sipi::ffi {

/*! Decide how to serve an IIIF image request. Reads engine services + config
 *  from `eng`. `cancelled` is polled between the decode stages (mirrors the
 *  legacy `peerConnected()` checks) — pass `[]{ return false; }` in tests.
 *  Returns the error status the caller renders, or `SipiStatus::ClientGone`
 *  when the client vanished mid-decode (the caller emits nothing). */
[[nodiscard]] std::expected<ServeResponse, SipiStatus>
  decide_serve_image(const SipiServeRequest &req, const EngineContext &eng, const std::function<bool()> &cancelled);

}// namespace Sipi::ffi

#endif// SIPI_FFI_SERVE_IMAGE_H
