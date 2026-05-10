/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_OBSERVABILITY_SENTRY_H
#define SIPI_OBSERVABILITY_SENTRY_H

#include <string>

#include <sentry.h>

#include "iiifparser/SipiQualityFormat.h"

namespace Sipi {
class SipiImage;
}// namespace Sipi

namespace Sipi::observability {

/*!
 * Operating mode for Sentry flush behavior.
 * CLI mode blocks briefly to ensure events are sent before process exit.
 * Server mode uses non-blocking flush to avoid stalling request threads.
 */
enum class SipiMode { CLI, Server };

/*!
 * Context about an image being processed, used for Sentry error reporting.
 * Fields may be empty/zero if the image was not successfully read.
 */
struct ImageContext
{
  std::string input_file;
  std::string output_file;
  std::string output_format;
  size_t width{0};
  size_t height{0};
  size_t channels{0};
  size_t bps{0};
  std::string colorspace;
  std::string icc_profile_type;
  std::string orientation;
  size_t file_size_bytes{0};
  std::string request_uri;///< IIIF request URI (server mode only)
};

/*!
 * Get the size of a file in bytes.
 * \param[in] path File path
 * \return File size in bytes, or 0 on failure
 */
size_t get_file_size(const std::string &path);

/*!
 * Convert a SipiQualityFormat::FormatType enum value to a human-readable string.
 */
std::string format_type_to_string(SipiQualityFormat::FormatType f);

/*!
 * Populate an ImageContext from a SipiImage.
 * Safe to call on partially-initialized images — getters return defaults (0) for unset fields.
 */
void populate_from_image(ImageContext &ctx, const SipiImage &img);

/*!
 * Capture an image processing error to Sentry with rich context.
 *
 * Thread-safe: all tags and context are attached directly to the event object
 * rather than the global scope, so concurrent calls from different request
 * threads cannot interfere with each other.
 *
 * Safe to call when Sentry is not initialized (sentry-native API calls no-op).
 *
 * \param[in] error_message The error message from the exception
 * \param[in] phase Processing phase: "read", "convert", or "write"
 * \param[in] ctx Image context with metadata about the image being processed
 * \param[in] mode Operating mode (default: CLI)
 * \param[in] level Sentry event level (default: SENTRY_LEVEL_ERROR)
 */
void capture_image_error(const std::string &error_message,
  const std::string &phase,
  const ImageContext &ctx,
  SipiMode mode = SipiMode::CLI,
  sentry_level_e level = SENTRY_LEVEL_ERROR);

}// namespace Sipi::observability

#endif// SIPI_OBSERVABILITY_SENTRY_H
