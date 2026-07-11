/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_POPULATE_FROM_IMAGE_H
#define SIPI_POPULATE_FROM_IMAGE_H

#include <cstddef>
#include <string>

namespace Sipi {
class SipiImage;
}// namespace Sipi

namespace Sipi::observability {

/*!
 * Context about an image being processed — reported as a handled-error
 * side-channel (the FFI seam's `SipiImageErrorReport`, CLI's JSON report via
 * `SipiReport.h`) rather than sent to any telemetry SDK directly. Fields may
 * be empty/zero if the image was not successfully read.
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
};

/*!
 * Get the size of a file in bytes.
 * \param[in] path File path
 * \return File size in bytes, or 0 on failure
 */
size_t get_file_size(const std::string &path);

/*!
 * Populate an ImageContext from a SipiImage.
 * Safe to call on partially-initialized images — getters return defaults (0) for unset fields.
 */
void populate_from_image(ImageContext &ctx, const SipiImage &img);

}// namespace Sipi::observability

#endif// SIPI_POPULATE_FROM_IMAGE_H
