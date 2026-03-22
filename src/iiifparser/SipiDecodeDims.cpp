/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "SipiDecodeDims.h"

#include <algorithm>
#include <cmath>

namespace Sipi {

DecodeDims compute_decode_dims(
    size_t src_w,
    size_t src_h,
    int clevels,
    const std::shared_ptr<SipiRegion> &region,
    const std::shared_ptr<SipiSize> &size)
{
  DecodeDims dims;

  // Step 1: Determine region in source coordinates
  size_t roi_w = src_w;
  size_t roi_h = src_h;

  if (region && region->getType() != SipiRegion::FULL) {
    int rx = 0, ry = 0;
    size_t rw = 0, rh = 0;
    region->crop_coords(src_w, src_h, rx, ry, rw, rh);
    dims.region_x = static_cast<size_t>(std::max(0, rx));
    dims.region_y = static_cast<size_t>(std::max(0, ry));
    dims.region_w = rw;
    dims.region_h = rh;
    roi_w = rw;
    roi_h = rh;
  }

  // Step 2: Compute reduce level + output dimensions from IIIF size
  // Pass clevels as the max reduce (matching JP2 reader behavior).
  int reduce = (clevels > 0) ? clevels : 0;
  bool redonly = true;
  size_t out_w = 0, out_h = 0;

  if (size && size->getType() != SipiSize::FULL) {
    // Make a copy of size to avoid mutating the original's internal state.
    SipiSize size_copy(*size);
    size_copy.get_size(roi_w, roi_h, out_w, out_h, reduce, redonly);
  } else {
    reduce = 0;
  }

  if (reduce < 0) reduce = 0;

  dims.reduce = reduce;
  dims.reduce_only = redonly;
  dims.out_w = out_w;
  dims.out_h = out_h;

  // Step 3: Compute actual decode buffer dimensions.
  // After Kakadu applies reduce + ROI, the decode buffer is:
  //   ceil(roi_dim / 2^reduce)
  // This matches codestream.get_dims() after apply_input_restrictions().
  size_t divisor = static_cast<size_t>(1) << reduce;
  dims.width = static_cast<size_t>(std::ceil(static_cast<double>(roi_w) / static_cast<double>(divisor)));
  dims.height = static_cast<size_t>(std::ceil(static_cast<double>(roi_h) / static_cast<double>(divisor)));

  // If size is FULL/undefined, output dims match decode dims
  if (out_w == 0) dims.out_w = dims.width;
  if (out_h == 0) dims.out_h = dims.height;

  return dims;
}

}// namespace Sipi
