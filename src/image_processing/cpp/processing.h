/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * Free-function pixel operators over `SipiImage` (see `//src/image_processing`'s
 * BUILD docstring for the package boundary). Every operator here reads and
 * mutates its `SipiImage &` argument through the class's public accessor
 * surface (`getNx`/`getNy`/`getNc`/`getBps`, `pixels_writable`/`pixels_view`,
 * `set_pixels`), never a protected member directly.
 */
#ifndef IMAGE_PROCESSING_PROCESSING_H
#define IMAGE_PROCESSING_PROCESSING_H

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "error/SipiValueError.h"
#include "image/SipiImage.h"

namespace Sipi {

// Reads a watermark TIFF into a grayscale byte buffer. `Sipi::processing::add_watermark`
// calls this; the definition lives in //src/format_handlers (SipiIOTiff.cpp,
// which uses libtiff) and is resolved at link time — this package does not
// depend on `//src/format_handlers` (see this package's BUILD docstring),
// keeping the one-way edge intact.
std::vector<unsigned char> read_watermark(const std::string &wmfile, int &nx, int &ny, int &nc);

namespace processing {

/*!
 * Crops an image to a region.
 *
 * \param[in] img Image to crop, mutated in place
 * \param[in] x Horizontal start position of region. If negative, it's set to 0, and the width is adjusted
 * \param[in] y Vertical start position of region. If negative, it's set to 0, and the height is adjusted
 * \param[in] width Width of the region. If the region goes beyond the image dimensions, it's adjusted.
 * \param[in] height Height of the region. If the region goes beyond the image dimensions, it's adjusted
 * \returns an error if the requested region is degenerate (e.g. entirely
 *          outside the image) or the image's bits/sample is not 8 or 16,
 *          success otherwise (including the case where the region already
 *          covers the whole image and no crop is needed)
 */
[[nodiscard]] Result<void> crop(SipiImage &img, int x, int y, size_t width = 0, size_t height = 0);

/*!
 * Crops an image to a region.
 *
 * \param[in] img Image to crop, mutated in place
 * \param[in] region Pointer to SipiRegion
 * \returns an error if the requested region is degenerate or the image's
 *          bits/sample is not 8 or 16, success otherwise
 */
[[nodiscard]] Result<void> crop(SipiImage &img, const std::shared_ptr<SipiRegion> &region);

/*!
 * Resize an image using a high speed algorithm which may result in poor image quality
 *
 * \param[in] img Image to resize, mutated in place
 * \param[in] nnx New horizontal dimension (width)
 * \param[in] nny New vertical dimension (height)
 */
bool scaleFast(SipiImage &img, size_t nnx, size_t nny);

/*!
 * Resize an image using some balance between speed and quality
 *
 * \param[in] img Image to resize, mutated in place
 * \param[in] nnx New horizontal dimension (width)
 * \param[in] nny New vertical dimension (height)
 */
bool scaleMedium(SipiImage &img, size_t nnx, size_t nny);

/*!
 * Resize an image using the best (but slow) algorithm
 *
 * \param[in] img Image to resize, mutated in place
 * \param[in] nnx New horizontal dimension (width)
 * \param[in] nny New vertical dimension (height)
 */
bool scale(SipiImage &img, size_t nnx = 0, size_t nny = 0);

/*!
 * Rotate an image
 *
 * The angles 0, 90, 180, 270 are treated specially!
 *
 * \param[in] img Image to rotate, mutated in place
 * \param[in] angle Rotation angle
 * \param[in] mirror If true, mirror the image before rotation
 */
bool rotate(SipiImage &img, float angle, bool mirror = false);

/*!
 * Rotate the image if necessary so that it has TOPLEFT orientation
 *
 * \param[in] img Image to reorient, mutated in place
 * @return Returns true on success, false on error
 */
bool set_topleft(SipiImage &img);

/*!
 * Convert full range YCbCr (YCC) to RGB colors
 *
 * \param[in] img Image to convert, mutated in place
 */
void convertYCC2RGB(SipiImage &img);

/*!
 * Converts the image representation
 *
 * \param[in] img Image to convert, mutated in place
 * \param[in] target_icc_p ICC profile which determines the new image representation
 * \param[in] bps Bits/sample of the new image representation
 */
void convertToIcc(SipiImage &img, const Icc &target_icc_p, int bps);

/*!
 * Removes a channel from a multi component image
 *
 * \param[in] img Image to modify, mutated in place
 * \param[in] channel Index of component to remove, starting with 0
 * \param[in] force_gray_alpha If true,  based on the alpha channel that is removed, a gray value is applied
 * to the remaining channels. This is useful for image formats that don't support alpha channel and where the
 * main content is black, so it is better separated from the background (as the default would be black).
 */
void removeChannel(SipiImage &img, unsigned int channel, bool force_gray_alpha = false);

/*!
 * Remove extra samples from the image. Some output formats support only 3 channels (e.g., JPEG)
 * so we need to remove the alpha channel. If we are dealing with a CMYK image, we need to take
 * this into account as well.
 *
 * \param[in] img Image to modify, mutated in place
 */
void removeExtraSamples(SipiImage &img, bool force_gray_alpha = false);

/*!
 * Convert an image from 16 to 8 bit. The algorithm just divides all pixel values
 * by 256 using the ">> 8" operator (fast & efficient)
 *
 * \param[in] img Image to convert, mutated in place
 */
void to8bps(SipiImage &img);

/*!
 * Convert an image to a bitonal representation using Steinberg-Floyd dithering.
 *
 * The method does nothing if the image is already bitonal. Otherwise, the image is converted
 * into a gray value image if necessary and then a FLoyd-Steinberg dithering is applied.
 *
 * \param[in] img Image to convert, mutated in place
 */
void toBitonal(SipiImage &img);

/*!
 * Add a watermark to an image.
 *
 * \param[in] img Image to watermark, mutated in place
 * \param[in] wmfilename Path to watermarkfile (which must be a TIFF file at the moment)
 */
void add_watermark(SipiImage &img, const std::string &wmfilename);

/*!
 * Conclude similarity of two SipiImages, used in tests. Only tested with small differences.
 *
 * \param[in] lhs left-hand side image to compare
 * \param[in] rhs right-hand side image to compare
 * \returns Returns similarity index, returns in [0..1]
 */
std::optional<double> compare(const SipiImage &lhs, const SipiImage &rhs);

/*!
 * Computes the per-channel absolute pixel-difference statistics between two
 * images. Unlike `operator-=` (which rescales the signed diff into a
 * displayable visualization), this reads the raw samples and reports the
 * true mean and maximum |Δ| plus the location of the maximum. Used by
 * `sipi compare` as the codec-rebaseline tolerance metric.
 *
 * \param[in] lhs left-hand side image
 * \param[in] rhs right-hand side image to compare against
 * \returns the difference statistics, or nullopt if the images are not
 *          comparable (differing dimensions, channels, bit depth, or photometric interpretation)
 */
[[nodiscard]] std::optional<PixelDelta> maxPixelDelta(const SipiImage &lhs, const SipiImage &rhs);

// ---------------------------------------------------------------------------
// Package-internal: not part of the public //src/image_processing surface.
// Bilinear-interpolation helpers shared between geometry.cpp
// (scaleMedium/rotate) and compose.cpp (add_watermark); declared here only so
// both translation units see the same signature.
// ---------------------------------------------------------------------------
byte bilinn(const byte buf[], int nx, int ny, double x, double y, int c, int n);
word bilinn(const word buf[], int nx, int ny, double x, double y, int c, int n);

}// namespace processing

/*!
 * Calculates the difference between 2 images.
 *
 * The difference between 2 images can contain (and usually will) negative values.
 * In order to create a standard image, the values at "0" will be lifted to 127 (8-bit images)
 * or 32767. The span will be defined by max(minimum, maximum), where minimum and maximum are
 * absolute values. Thus a new pixelvalue will be calculated as follows:
 * ```
 * int maxmax = abs(min) > abs(max) ? abs(min) : abs(min);
 * newval = (byte) ((oldval + maxmax)*UCHAR_MAX/(2*maxmax));
 * ```
 * \param[in] lhs left-hand side of "-=", mutated in place
 * \param[in] rhs right hand side of "-="
 */
SipiImage &operator-=(SipiImage &lhs, const SipiImage &rhs);

/*!
 * Calculates the difference between 2 images.
 *
 * \param[in] lhs left-hand side of "-" operator
 * \param[in] rhs right hand side of "-" operator
 */
SipiImage operator-(const SipiImage &lhs, const SipiImage &rhs);

SipiImage &operator+=(SipiImage &lhs, const SipiImage &rhs);

SipiImage operator+(const SipiImage &lhs, const SipiImage &rhs);

bool operator==(const SipiImage &lhs, const SipiImage &rhs);

}// namespace Sipi

#endif
