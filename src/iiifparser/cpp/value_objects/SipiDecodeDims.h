/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_SIPIDECODEDIMS_H
#define SIPI_SIPIDECODEDIMS_H

#include <cstddef>
#include <memory>

#include "SipiRegion.h"
#include "SipiSize.h"

namespace Sipi {

class SipiImgInfo;

/// Actual decode dimensions after applying the IIIF Region + Size at the
/// negotiated Decode level.
/// Used by: memory budget estimation, JP2 read, future pyramidal TIFF read.
struct DecodeDims {
  size_t width{0};       ///< decode buffer width (after the Decode level + Region)
  size_t height{0};      ///< decode buffer height (after the Decode level + Region)
  int reduce{0};         ///< DWT reduce level = Decode level (0 = full resolution)
  bool reduce_only{false}; ///< true if scaling can be done entirely at the Decode level (no post-scale needed)
  size_t out_w{0};       ///< requested output width (after scale, before rotation)
  size_t out_h{0};       ///< requested output height (after scale, before rotation)
  size_t region_x{0};    ///< Region origin x in source coords
  size_t region_y{0};    ///< Region origin y in source coords
  size_t region_w{0};    ///< Region width in source coords (0 = full width)
  size_t region_h{0};    ///< Region height in source coords (0 = full height)
};

/// Compute actual decode dimensions given source info + the IIIF Region/Size.
///
/// Mirrors the logic in SipiIOJ2k::read():
/// 1. region->crop_coords() to get the Region in source coords
/// 2. size->get_size(roi_dims) to compute the Decode level + output dims
/// 3. Decode dims = ceil(roi_dims / 2^reduce)
///
/// @param src_w     Source image width (from read_shape)
/// @param src_h     Source image height (from read_shape)
/// @param clevels   Max DWT levels (from read_shape, -1 if not applicable)
/// @param region    IIIF region parameter (may be nullptr for FULL)
/// @param size      IIIF size parameter (may be nullptr for FULL)
/// @return DecodeDims with actual decode buffer dimensions
[[nodiscard]] DecodeDims compute_decode_dims(
    size_t src_w,
    size_t src_h,
    int clevels,
    const std::shared_ptr<SipiRegion> &region,
    const std::shared_ptr<SipiSize> &size);

}// namespace Sipi

#endif// SIPI_SIPIDECODEDIMS_H
