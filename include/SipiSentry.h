/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPISENTRY_H
#define SIPISENTRY_H

#include <string>
#include <sys/stat.h>

#include <sentry.h>

#include "SipiImage.hpp"
#include "iiifparser/SipiQualityFormat.h"
#include "metadata/SipiIcc.h"

namespace Sipi {

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
inline size_t get_file_size(const std::string &path)
{
  struct stat st{};
  if (stat(path.c_str(), &st) == 0) {
    return static_cast<size_t>(st.st_size);
  }
  return 0;
}

/*!
 * Convert a PredefinedProfiles enum value to a human-readable string.
 */
inline std::string predefined_profile_to_string(PredefinedProfiles p)
{
  switch (p) {
  case icc_undefined: return "undefined";
  case icc_unknown: return "unknown/embedded";
  case icc_sRGB: return "sRGB";
  case icc_AdobeRGB: return "AdobeRGB";
  case icc_RGB: return "RGB (custom)";
  case icc_CMYK_standard: return "CMYK (USWebCoatedSWOP)";
  case icc_GRAY_D50: return "Gray D50";
  case icc_LUM_D65: return "Luminance D65";
  case icc_ROMM_GRAY: return "ROMM Gray";
  case icc_LAB: return "L*a*b*";
  default: return "unknown";
  }
}

/*!
 * Convert an Orientation enum value to a human-readable string.
 */
inline std::string orientation_to_string(Orientation o)
{
  switch (o) {
  case TOPLEFT: return "TOPLEFT";
  case TOPRIGHT: return "TOPRIGHT";
  case BOTRIGHT: return "BOTRIGHT";
  case BOTLEFT: return "BOTLEFT";
  case LEFTTOP: return "LEFTTOP";
  case RIGHTTOP: return "RIGHTTOP";
  case RIGHTBOT: return "RIGHTBOT";
  case LEFTBOT: return "LEFTBOT";
  default: return "unknown";
  }
}

/*!
 * Convert a SipiQualityFormat::FormatType enum value to a human-readable string.
 */
inline std::string format_type_to_string(SipiQualityFormat::FormatType f)
{
  switch (f) {
  case SipiQualityFormat::JPG: return "jpg";
  case SipiQualityFormat::TIF: return "tif";
  case SipiQualityFormat::PNG: return "png";
  case SipiQualityFormat::JP2: return "jp2";
  case SipiQualityFormat::GIF: return "gif";
  case SipiQualityFormat::PDF: return "pdf";
  case SipiQualityFormat::WEBP: return "webp";
  default: return "unsupported";
  }
}

/*!
 * Populate an ImageContext from a SipiImage.
 * Safe to call on partially-initialized images — getters return defaults (0) for unset fields.
 */
inline void populate_from_image(ImageContext &ctx, const SipiImage &img)
{
  ctx.width = img.getNx();
  ctx.height = img.getNy();
  ctx.channels = img.getNc();
  ctx.bps = img.getBps();
  ctx.colorspace = to_string(img.getPhoto());
  ctx.orientation = orientation_to_string(img.getOrientation());

  auto icc = img.getIcc();
  if (icc) {
    ctx.icc_profile_type = predefined_profile_to_string(icc->getProfileType());
  }
}

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
inline void capture_image_error(const std::string &error_message,
  const std::string &phase,
  const ImageContext &ctx,
  SipiMode mode = SipiMode::CLI,
  sentry_level_e level = SENTRY_LEVEL_ERROR)
{
  const char *mode_str = (mode == SipiMode::CLI) ? "cli" : "server";

  sentry_value_t event = sentry_value_new_event();
  sentry_value_set_by_key(event, "level", sentry_value_new_string(
    level == SENTRY_LEVEL_ERROR ? "error" :
    level == SENTRY_LEVEL_WARNING ? "warning" : "fatal"));

  // Set the message
  sentry_value_t message = sentry_value_new_object();
  sentry_value_set_by_key(message, "formatted", sentry_value_new_string(error_message.c_str()));
  sentry_value_set_by_key(event, "message", message);

  // Set searchable tags directly on the event (not the global scope) for thread safety.
  sentry_value_t tags = sentry_value_new_object();
  sentry_value_set_by_key(tags, "sipi.mode", sentry_value_new_string(mode_str));
  sentry_value_set_by_key(tags, "sipi.phase", sentry_value_new_string(phase.c_str()));
  if (!ctx.output_format.empty()) {
    sentry_value_set_by_key(tags, "sipi.output_format", sentry_value_new_string(ctx.output_format.c_str()));
  }
  if (!ctx.colorspace.empty()) {
    sentry_value_set_by_key(tags, "sipi.colorspace", sentry_value_new_string(ctx.colorspace.c_str()));
  }
  if (ctx.bps > 0) {
    sentry_value_set_by_key(tags, "sipi.bps", sentry_value_new_string(std::to_string(ctx.bps).c_str()));
  }
  if (!ctx.request_uri.empty()) {
    sentry_value_set_by_key(tags, "sipi.request_uri", sentry_value_new_string(ctx.request_uri.c_str()));
  }
  sentry_value_set_by_key(event, "tags", tags);

  // Build "Image" context with full details
  sentry_value_t image_ctx = sentry_value_new_object();
  sentry_value_set_by_key(image_ctx, "type", sentry_value_new_string("image"));

  sentry_value_t image_data = sentry_value_new_object();
  if (!ctx.input_file.empty()) {
    sentry_value_set_by_key(image_data, "input_file", sentry_value_new_string(ctx.input_file.c_str()));
  }
  if (!ctx.output_file.empty()) {
    sentry_value_set_by_key(image_data, "output_file", sentry_value_new_string(ctx.output_file.c_str()));
  }
  if (!ctx.output_format.empty()) {
    sentry_value_set_by_key(image_data, "output_format", sentry_value_new_string(ctx.output_format.c_str()));
  }
  if (ctx.width > 0 || ctx.height > 0) {
    sentry_value_set_by_key(image_data, "width", sentry_value_new_int32(static_cast<int32_t>(ctx.width)));
    sentry_value_set_by_key(image_data, "height", sentry_value_new_int32(static_cast<int32_t>(ctx.height)));
  }
  if (ctx.channels > 0) {
    sentry_value_set_by_key(image_data, "channels", sentry_value_new_int32(static_cast<int32_t>(ctx.channels)));
  }
  if (ctx.bps > 0) {
    sentry_value_set_by_key(image_data, "bps", sentry_value_new_int32(static_cast<int32_t>(ctx.bps)));
  }
  if (!ctx.colorspace.empty()) {
    sentry_value_set_by_key(image_data, "colorspace", sentry_value_new_string(ctx.colorspace.c_str()));
  }
  if (!ctx.icc_profile_type.empty()) {
    sentry_value_set_by_key(image_data, "icc_profile_type", sentry_value_new_string(ctx.icc_profile_type.c_str()));
  }
  if (!ctx.orientation.empty()) {
    sentry_value_set_by_key(image_data, "orientation", sentry_value_new_string(ctx.orientation.c_str()));
  }
  if (ctx.file_size_bytes > 0) {
    sentry_value_set_by_key(image_data, "file_size_bytes",
      sentry_value_new_double(static_cast<double>(ctx.file_size_bytes)));
  }
  if (!ctx.request_uri.empty()) {
    sentry_value_set_by_key(image_data, "request_uri", sentry_value_new_string(ctx.request_uri.c_str()));
  }

  sentry_value_set_by_key(image_ctx, "data", image_data);

  // Add the Image context to the event
  sentry_value_t contexts = sentry_value_new_object();
  sentry_value_set_by_key(contexts, "Image", image_ctx);
  sentry_value_set_by_key(event, "contexts", contexts);

  sentry_capture_event(event);

  // In CLI mode, block briefly to ensure the event is sent before process exit.
  // In server mode, use non-blocking flush to avoid stalling request threads.
  sentry_flush(mode == SipiMode::CLI ? 2000 : 0);
}

}// namespace Sipi

#endif// SIPISENTRY_H
