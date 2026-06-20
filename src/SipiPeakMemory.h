/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_SIPIPEAKMEMORY_H
#define SIPI_SIPIPEAKMEMORY_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace Sipi {

/// Estimate peak memory for a complete IIIF processing pipeline.
///
/// Uses actual decode dimensions (with reduce levels + ROI), not full source.
/// Walks the pipeline stages and returns the maximum (old_buffer + new_buffer)
/// at any point:
///   read → scale → rotate → ICC conversion
///
/// Note: for JP2/pyramidal TIFF, region extraction happens INSIDE the decoder
/// (via ROI restriction), so there is no separate crop buffer — decode_w/h
/// already reflect the cropped region at the reduce level.
///
/// @param decode_w     Decode buffer width (from compute_decode_dims)
/// @param decode_h     Decode buffer height
/// @param out_w        Output width after scale (0 = no scaling)
/// @param out_h        Output height after scale (0 = no scaling)
/// @param nc           Number of channels (from SipiImgInfo, 0 defaults to 4)
/// @param bps          Bits per sample (from SipiImgInfo, 0 defaults to 8)
/// @param rotation     Rotation angle in degrees
/// @param needs_icc    Whether ICC conversion will run
/// @return Estimated peak memory in bytes
[[nodiscard]] inline size_t estimate_peak_memory(
    size_t decode_w,
    size_t decode_h,
    size_t out_w,
    size_t out_h,
    int nc,
    int bps,
    double rotation,
    bool needs_icc)
{
  static_assert(sizeof(size_t) >= 8, "Memory estimation requires 64-bit size_t");

  // Defaults for missing metadata
  size_t channels = (nc > 0) ? static_cast<size_t>(nc) : 4;
  size_t bytes_per_sample = (bps > 0) ? static_cast<size_t>(bps) / 8 : 1;
  if (bytes_per_sample == 0) bytes_per_sample = 1;
  size_t bytes_per_pixel = channels * bytes_per_sample;

  // Saturating buffer size computation — clamp to SIZE_MAX on overflow.
  // Prevents a wrapped-to-small estimate from bypassing the budget.
  constexpr size_t kMaxBuf = std::numeric_limits<size_t>::max();
  auto safe_buf = [](size_t w, size_t h, size_t bpp) -> size_t {
    if (w == 0 || h == 0) return 0;
    if (w > kMaxBuf / h) return kMaxBuf;
    size_t pixels = w * h;
    if (pixels > kMaxBuf / bpp) return kMaxBuf;
    return pixels * bpp;
  };

  // Decode buffer: actual dimensions after reduce + ROI
  size_t buf_decode = safe_buf(decode_w, decode_h, bytes_per_pixel);

  // Scale: 0 means no scaling (output = decode dims)
  if (out_w == 0) out_w = decode_w;
  if (out_h == 0) out_h = decode_h;
  size_t buf_scaled = safe_buf(out_w, out_h, bytes_per_pixel);

  // Rotate: 90/270 swap dims, arbitrary angles expand to diagonal
  size_t rot_w = out_w, rot_h = out_h;
  int rot_int = static_cast<int>(rotation) % 360;
  if (rot_int < 0) rot_int += 360;
  if (rot_int == 90 || rot_int == 270) {
    std::swap(rot_w, rot_h);
  } else if (rot_int != 0 && rot_int != 180) {
    // Arbitrary angle: bounding box of rotated rectangle
    double diag = std::sqrt(
        static_cast<double>(out_w) * static_cast<double>(out_w)
        + static_cast<double>(out_h) * static_cast<double>(out_h));
    rot_w = rot_h = static_cast<size_t>(std::ceil(diag));
  }
  size_t buf_rotated = safe_buf(rot_w, rot_h, bytes_per_pixel);

  // ICC conversion: channels may change but dimensions don't
  size_t buf_final = needs_icc ? safe_buf(rot_w, rot_h, bytes_per_pixel) : 0;

  // Peak = max of any (old + new) pair across pipeline stages.
  // Each step allocates a new buffer and frees the old one. Peak is max(old+new)
  // at any single transition. Only include steps that actually run.
  bool needs_scale = (out_w != decode_w || out_h != decode_h);
  bool needs_rotate = (rot_int != 0);

  size_t peak = buf_decode;// baseline: first allocation

  if (needs_scale) {
    // scale() can use 2-stage downscale: ~1.5x intermediate buffer
    size_t scale_peak = buf_decode + buf_scaled + buf_scaled / 2;
    peak = std::max(peak, scale_peak);
  }

  if (needs_rotate) {
    // Input to rotate is buf_scaled (or buf_decode if no scaling)
    size_t rotate_input = needs_scale ? buf_scaled : buf_decode;
    peak = std::max(peak, rotate_input + buf_rotated);
  }

  if (needs_icc) {
    // Input to ICC is buf_rotated (or buf_scaled/buf_decode if no rotate)
    size_t icc_input = needs_rotate ? buf_rotated : (needs_scale ? buf_scaled : buf_decode);
    peak = std::max(peak, icc_input + buf_final);
  }

  return peak;
}

}// namespace Sipi

#endif// SIPI_SIPIPEAKMEMORY_H
