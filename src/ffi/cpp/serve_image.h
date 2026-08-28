/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * The IIIF image pipeline behind the FFI seam (strangler-fig).
 *
 * `build_image_response` builds the transport-pure response for an IIIF image
 * request — the `serve_iiif` decode path. It
 * reconstructs the typed IIIF params from the flat `SipiServeRequest`, runs
 * admission (cache / memory-budget), builds the canonical URL, and
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
#include <string>

#include "error/SipiValueError.h"
#include "ffi/engine_context.h"
#include "ffi/serve_response.h"
#include "ffi/sipi_ffi.h"
#include "image/populate_from_image.h"

namespace Sipi::ffi {

/*! Build the response for an IIIF image request. Reads engine services + config
 *  from `eng`. `cancelled` is polled between the decode stages to abort when the
 *  client has disconnected — pass `[]{ return false; }` in tests.
 *  Returns the error status the caller renders, or `SipiStatus::ClientGone`
 *  when the client vanished mid-decode (the caller emits nothing). */
[[nodiscard]] std::expected<ServeResponse, SipiStatus>
  build_image_response(const SipiServeRequest &req, const EngineContext &eng, const std::function<bool()> &cancelled);

/*! The seam's `SipiStatus` for a `SipiValueError`, derived from
 *  `policy_for(err.code()).http_status_class` (never a raw `ErrorCode` switch —
 *  the policy table is the single source of truth). */
[[nodiscard]] SipiStatus status_for(const SipiValueError &err);

/*! Reports a `SipiValueError` through the seam's `SipiImageErrorReport` side
 *  channel, honouring `policy_for(err.code()).sentry_policy`: a `kSkip` policy
 *  marks a failure that is not a server-side fault — a client abort mid-response
 *  is the motivating case — so it returns without calling `report_error`. */
void report_value_error(SipiReportErrorFn report_error,
  void *report_ctx,
  const SipiValueError &err,
  const std::string &phase,
  const observability::ImageContext &ctx);

}// namespace Sipi::ffi

#endif// SIPI_FFI_SERVE_IMAGE_H
