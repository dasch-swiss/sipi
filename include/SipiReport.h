/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPIREPORT_H
#define SIPIREPORT_H

#include <optional>
#include <ostream>
#include <string>

#include "populate_from_image.h"

namespace Sipi {

/*!
 * Emit a structured JSON report mirroring an ImageContext to the given
 * output stream. Used by the `--json` CLI flag to give a full diagnostic
 * payload independent of any telemetry backend.
 *
 * The produced document is a single JSON object terminated by a trailing
 * newline. The schema matches the ImageContext struct in populate_from_image.h.
 *
 * On success (no error_message / no phase): `status == "ok"` and the `image`
 * object is populated from the ImageContext.
 *
 * On error (`error_message` set): `status == "error"`, `phase` (if supplied)
 * and `error_message` are emitted. If `phase == "cli_args"` the `image`
 * object is omitted entirely (no image was loaded); otherwise the `image`
 * object is still emitted and contains whatever the ImageContext captured
 * before the failure point.
 *
 * \param[out] out             Output stream (typically std::cout)
 * \param[in]  ctx             Image context with metadata about the image
 * \param[in]  error_message   If present, renders the document as an error
 * \param[in]  phase           "cli_args" | "read" | "convert" | "write"
 */
void emit_json_report(std::ostream &out,
  const observability::ImageContext &ctx,
  std::optional<std::string> error_message = std::nullopt,
  std::optional<std::string> phase = std::nullopt);

/*!
 * Emit a minimal `phase: "cli_args"` error JSON for CLI argument validation
 * failures that fire before any image is loaded. The `image` object is
 * omitted entirely because no image processing was attempted.
 *
 * \param[out] out  Output stream (typically std::cout)
 * \param[in]  err  Error message describing the argument failure
 */
void emit_json_cli_arg_error(std::ostream &out, const std::string &err);

}// namespace Sipi

#endif// SIPIREPORT_H
