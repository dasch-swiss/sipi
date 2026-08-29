/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "processing.h"
#include "resample.h"

#include "error/SipiValueError.h"
#include "iiifparser/SipiRegion.h"
#include "observability/profiling.h"
#include "util/checked_arith.h"

namespace Sipi {

namespace {

// Fixed-point one (weights sum to this per output). kResamplePrecisionBits and
// the accumulation kernel live in resample.h / resample.cc; the builder below
// only produces the integer weights the kernel consumes.
constexpr int32_t kResampleOne = 1 << kResamplePrecisionBits;

// Separable resample weights for one axis, in CSR layout: output sample `i`
// is the weighted sum of source samples idx[offset[i] .. offset[i+1]). The
// per-output weights are fixed-point and sum to exactly kResampleOne. Shrinking
// uses area (box) averaging for an anti-aliased downscale; enlarging uses 2-tap
// linear interpolation; an unchanged axis is an identity passthrough.
struct AxisWeights
{
  std::vector<size_t> offset;// dst + 1 row pointers into idx/wt
  std::vector<size_t> idx;   // source sample indices
  std::vector<int32_t> wt;   // fixed-point weights (each output's weights sum to kResampleOne)
};

AxisWeights build_axis_weights(size_t src, size_t dst)
{
  AxisWeights w;
  std::vector<double> dwt;// real-valued weights, quantised to fixed point below
  w.offset.reserve(dst + 1);

  if (dst == src) {
    for (size_t i = 0; i < dst; ++i) {
      w.offset.push_back(w.idx.size());
      w.idx.push_back(i);
      dwt.push_back(1.0);
    }
  } else if (dst < src) {
    // Area averaging: output i covers the source interval [i*ratio, (i+1)*ratio);
    // each covered source sample contributes its overlap length, normalised by
    // the interval width so the weights sum to 1.
    const double ratio = static_cast<double>(src) / static_cast<double>(dst);
    for (size_t i = 0; i < dst; ++i) {
      w.offset.push_back(w.idx.size());
      const double start = static_cast<double>(i) * ratio;
      const double end = static_cast<double>(i + 1) * ratio;
      for (auto s = static_cast<size_t>(std::floor(start)); static_cast<double>(s) < end && s < src; ++s) {
        const double lo = std::max(start, static_cast<double>(s));
        const double hi = std::min(end, static_cast<double>(s + 1));
        const double cover = hi - lo;
        if (cover <= 0.0) { continue; }
        w.idx.push_back(s);
        dwt.push_back(cover / ratio);
      }
    }
  } else {
    // Enlarge: 2-tap linear interpolation. The sample position matches the
    // legacy mapping i*(src-1)/(dst-1), so an exact integer upscale lands on
    // grid points.
    for (size_t i = 0; i < dst; ++i) {
      w.offset.push_back(w.idx.size());
      const double pos =
        (dst > 1) ? static_cast<double>(i) * static_cast<double>(src - 1) / static_cast<double>(dst - 1) : 0.0;
      const auto s0 = static_cast<size_t>(std::floor(pos));
      const double frac = pos - static_cast<double>(s0);
      const size_t s1 = std::min(s0 + 1, src - 1);
      w.idx.push_back(s0);
      dwt.push_back(1.0 - frac);
      if (s1 != s0) {
        w.idx.push_back(s1);
        dwt.push_back(frac);
      }
    }
  }
  w.offset.push_back(w.idx.size());

  // Quantise each output's real weights to fixed point, then fold the rounding
  // residual into the largest tap so the group sums to exactly kResampleOne
  // (preserves the DC level: a flat region resamples to itself).
  w.wt.resize(dwt.size());
  for (size_t i = 0; i + 1 < w.offset.size(); ++i) {
    int32_t sum = 0;
    size_t max_t = w.offset[i];
    for (size_t t = w.offset[i]; t < w.offset[i + 1]; ++t) {
      w.wt[t] = static_cast<int32_t>(std::lround(dwt[t] * kResampleOne));
      sum += w.wt[t];
      if (dwt[t] > dwt[max_t]) { max_t = t; }
    }
    if (w.offset[i] < w.offset[i + 1]) { w.wt[max_t] += kResampleOne - sum; }
  }
  return w;
}

}// namespace

namespace processing {

// Bilinear-interpolation helpers for `scaleMedium`/`rotate` below. External
// linkage (not file-local): `compose.cpp`'s `add_watermark` also calls the
// byte overload; both are declared in the package-internal section of
// processing.h.
/****************************************************************************/
#define POSITION(x, y, c, n) ((n) * ((y)*nx + (x)) + c)

byte bilinn(const byte buf[], const int nx, const int ny, const double x, const double y, const int c, const int n)
{
  // Degenerate buffer — interpolation needs at least 2x2
  if (nx < 2 || ny < 2) { return buf[c]; }

  auto ix = static_cast<int>(x);
  auto iy = static_cast<int>(y);

  // Clamp to valid interpolation range: ix in [0, nx-2], iy in [0, ny-2]
  if (ix < 0) { ix = 0; }
  if (iy < 0) { iy = 0; }
  if (ix >= nx - 1) { ix = nx - 2; }
  if (iy >= ny - 1) { iy = ny - 2; }

  const auto rx = std::clamp(x - static_cast<double>(ix), 0.0, 1.0);
  const auto ry = std::clamp(y - static_cast<double>(iy), 0.0, 1.0);

  constexpr double Threshold = 1.0e-2;

  if ((rx < Threshold) && (ry < Threshold)) { return (buf[POSITION(ix, iy, c, n)]); }

  if (rx < Threshold) {
    return ((byte)lround(((double)buf[POSITION(ix, iy, c, n)] * (1 - rx - ry + rx * ry)
                          + (double)buf[POSITION(ix, (iy + 1), c, n)] * (ry - rx * ry))));
  }

  if (ry < Threshold) {
    return ((byte)lround(((double)buf[POSITION(ix, iy, c, n)] * (1 - rx - ry + rx * ry)
                          + (double)buf[POSITION((ix + 1), iy, c, n)] * (rx - rx * ry))));
  }

  return ((byte)lround(((double)buf[POSITION(ix, iy, c, n)] * (1 - rx - ry + rx * ry)
                        + (double)buf[POSITION((ix + 1), iy, c, n)] * (rx - rx * ry)
                        + (double)buf[POSITION(ix, (iy + 1), c, n)] * (ry - rx * ry)
                        + (double)buf[POSITION((ix + 1), (iy + 1), c, n)] * rx * ry)));
}

/*==========================================================================*/

word bilinn(const word buf[], const int nx, const int ny, const double x, const double y, const int c, const int n)
{
  // Degenerate buffer — interpolation needs at least 2x2
  if (nx < 2 || ny < 2) { return buf[c]; }

  auto ix = static_cast<int>(x);
  auto iy = static_cast<int>(y);

  // Clamp to valid interpolation range: ix in [0, nx-2], iy in [0, ny-2]
  if (ix < 0) { ix = 0; }
  if (iy < 0) { iy = 0; }
  if (ix >= nx - 1) { ix = nx - 2; }
  if (iy >= ny - 1) { iy = ny - 2; }

  const auto rx = std::clamp(x - static_cast<double>(ix), 0.0, 1.0);
  const auto ry = std::clamp(y - static_cast<double>(iy), 0.0, 1.0);

  constexpr double Threshold = 1.0e-2;

  if ((rx < Threshold) && (ry < Threshold)) { return (buf[POSITION(ix, iy, c, n)]); }

  if (rx < Threshold) {
    return ((word)lround(((double)buf[POSITION(ix, iy, c, n)] * (1 - rx - ry + rx * ry)
                          + (double)buf[POSITION(ix, (iy + 1), c, n)] * (ry - rx * ry))));
  }

  if (ry < Threshold) {
    return ((word)lround(((double)buf[POSITION(ix, iy, c, n)] * (1 - rx - ry + rx * ry)
                          + (double)buf[POSITION((ix + 1), iy, c, n)] * (rx - rx * ry))));
  }

  return ((word)lround(((double)buf[POSITION(ix, iy, c, n)] * (1 - rx - ry + rx * ry)
                        + (double)buf[POSITION((ix + 1), iy, c, n)] * (rx - rx * ry)
                        + (double)buf[POSITION(ix, (iy + 1), c, n)] * (ry - rx * ry)
                        + (double)buf[POSITION((ix + 1), (iy + 1), c, n)] * rx * ry)));
}

/*==========================================================================*/

#undef POSITION

Result<void> crop(SipiImage &img, int x, int y, size_t width, size_t height)
{
  SIPI_ZONE_N("SipiImage::crop");
  const size_t img_nx = img.getNx();
  const size_t img_ny = img.getNy();

  // x/y are validated and clamped BEFORE combining with the unsigned width/
  // height: a negative x or y converted directly to size_t and added to
  // width/height would wrap, and the caller-supplied region could silently
  // widen back out to the full image instead of being rejected.
  if (x < 0) {
    const auto shrink_x = static_cast<size_t>(-static_cast<long long>(x));
    if (width != 0) {
      if (shrink_x >= width) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Crop region entirely left of the image (x=" + std::to_string(x) + ", width=" + std::to_string(width)
            + ")" });
      }
      width -= shrink_x;
    }
    x = 0;
  } else if (x >= (long)img_nx) {
    return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
      "Crop x=" + std::to_string(x) + " is outside the image (width=" + std::to_string(img_nx) + ")" });
  }

  if (y < 0) {
    const auto shrink_y = static_cast<size_t>(-static_cast<long long>(y));
    if (height != 0) {
      if (shrink_y >= height) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Crop region entirely above the image (y=" + std::to_string(y) + ", height=" + std::to_string(height)
            + ")" });
      }
      height -= shrink_y;
    }
    y = 0;
  } else if (y >= (long)img_ny) {
    return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
      "Crop y=" + std::to_string(y) + " is outside the image (height=" + std::to_string(img_ny) + ")" });
  }

  if (width == 0) {
    width = img_nx - x;
  } else if ((x + width) > img_nx) {
    width = img_nx - x;
  }

  if (height == 0) {
    height = img_ny - y;
  } else if ((y + height) > img_ny) {
    height = img_ny - y;
  }

  if ((x == 0) && (y == 0) && (width == img_nx) && (height == img_ny)) {
    return {};// we do not have to crop!!
  }

  const size_t nc = img.getNc();
  const size_t bps = img.getBps();

  if (bps == 8) {
    const byte *inbuf = img.pixels_view().data();
    const auto buf_size = checked_buf_size(width, height, nc, 1);
    if (!buf_size) {
      return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
        "Pixel buffer size overflow (dimensions=" + std::to_string(width) + "x" + std::to_string(height)
          + ", channels=" + std::to_string(nc) + ", elem=1)" });
    }
    std::vector<byte> outbuf(*buf_size);

    for (size_t j = 0; j < height; j++) {
      for (size_t i = 0; i < width; i++) {
        for (size_t k = 0; k < nc; k++) {
          outbuf[nc * (j * width + i) + k] = inbuf[nc * ((j + y) * img_nx + (i + x)) + k];
        }
      }
    }

    img.set_pixels(std::move(outbuf), width, height, nc, bps);
  } else if (bps == 16) {
    const word *inbuf = reinterpret_cast<const word *>(img.pixels_view().data());
    const auto buf_size = checked_buf_size(width, height, nc, 2);
    if (!buf_size) {
      return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
        "Pixel buffer size overflow (dimensions=" + std::to_string(width) + "x" + std::to_string(height)
          + ", channels=" + std::to_string(nc) + ", elem=2)" });
    }
    std::vector<byte> outbuf_v(*buf_size);
    word *outbuf = reinterpret_cast<word *>(outbuf_v.data());

    for (size_t j = 0; j < height; j++) {
      for (size_t i = 0; i < width; i++) {
        for (size_t k = 0; k < nc; k++) {
          outbuf[nc * (j * width + i) + k] = inbuf[nc * ((j + y) * img_nx + (i + x)) + k];
        }
      }
    }

    img.set_pixels(std::move(outbuf_v), width, height, nc, bps);
  } else {
    return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
      "Cannot crop image with unsupported bits/sample (" + std::to_string(bps) + "), only 8 and 16 are supported" });
  }

  return {};
}

//============================================================================

Result<void> crop(SipiImage &img, const std::shared_ptr<SipiRegion> &region)
{
  int x, y;
  size_t width, height;
  if (region->getType() == SipiRegion::FULL) {
    return {};// we do not have to crop;
  }
  const size_t img_nx = img.getNx();
  const size_t img_ny = img.getNy();
  region->crop_coords(img_nx, img_ny, x, y, width, height);

  const size_t nc = img.getNc();
  const size_t bps = img.getBps();

  if (bps == 8) {
    const byte *inbuf = img.pixels_view().data();
    const auto buf_size = checked_buf_size(width, height, nc, 1);
    if (!buf_size) {
      return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
        "Pixel buffer size overflow (dimensions=" + std::to_string(width) + "x" + std::to_string(height)
          + ", channels=" + std::to_string(nc) + ", elem=1)" });
    }
    std::vector<byte> outbuf(*buf_size);

    for (size_t j = 0; j < height; j++) {
      for (size_t i = 0; i < width; i++) {
        for (size_t k = 0; k < nc; k++) {
          outbuf[nc * (j * width + i) + k] = inbuf[nc * ((j + y) * img_nx + (i + x)) + k];
        }
      }
    }

    img.set_pixels(std::move(outbuf), width, height, nc, bps);
  } else if (bps == 16) {
    const word *inbuf = reinterpret_cast<const word *>(img.pixels_view().data());
    const auto buf_size = checked_buf_size(width, height, nc, 2);
    if (!buf_size) {
      return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
        "Pixel buffer size overflow (dimensions=" + std::to_string(width) + "x" + std::to_string(height)
          + ", channels=" + std::to_string(nc) + ", elem=2)" });
    }
    std::vector<byte> outbuf_v(*buf_size);
    word *outbuf = reinterpret_cast<word *>(outbuf_v.data());

    for (size_t j = 0; j < height; j++) {
      for (size_t i = 0; i < width; i++) {
        for (size_t k = 0; k < nc; k++) {
          outbuf[nc * (j * width + i) + k] = inbuf[nc * ((j + y) * img_nx + (i + x)) + k];
        }
      }
    }

    img.set_pixels(std::move(outbuf_v), width, height, nc, bps);
  } else {
    return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
      "Cannot crop image with unsupported bits/sample (" + std::to_string(bps) + "), only 8 and 16 are supported" });
  }

  return {};
}

//============================================================================

Result<void> scaleFast(SipiImage &img, size_t nnx, size_t nny)
{
  SIPI_ZONE_N("SipiImage::scaleFast");
  const size_t nx = img.getNx();
  const size_t ny = img.getNy();
  // A requested axis of one sample or fewer has no resampling to do; the
  // requested size is a malformed IIIF size parameter.
  if (nnx <= 1 || nny <= 1) {
    return std::unexpected(SipiValueError{ ErrorCode::kInvalidRequestParameter,
      "scaleFast: unsupported target dimensions (src=" + std::to_string(nx) + "x" + std::to_string(ny)
        + ", dst=" + std::to_string(nnx) + "x" + std::to_string(nny) + "): each axis must be at least 2 samples" });
  }
  // A source axis of one sample or fewer has no resampling to do; the source
  // dimensions are a property of the decoded file, not the request.
  if (nx <= 1 || ny <= 1) {
    return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
      "scaleFast: unsupported source dimensions (src=" + std::to_string(nx) + "x" + std::to_string(ny)
        + ", dst=" + std::to_string(nnx) + "x" + std::to_string(nny) + "): each axis must be at least 2 samples" });
  }

  auto xlut = std::make_unique<size_t[]>(nnx);
  auto ylut = std::make_unique<size_t[]>(nny);

  for (size_t i = 0; i < nnx; i++) { xlut[i] = (size_t)lround(i * (nx - 1) / (nnx - 1)); }
  for (size_t i = 0; i < nny; i++) { ylut[i] = (size_t)lround(i * (ny - 1) / (nny - 1)); }

  const size_t nc = img.getNc();
  const size_t bps = img.getBps();

  if (bps == 8) {
    const auto buf_size = checked_buf_size(nnx, nny, nc, 1);
    if (!buf_size) {
      return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
        "Pixel buffer size overflow (dimensions=" + std::to_string(nnx) + "x" + std::to_string(nny)
          + ", channels=" + std::to_string(nc) + ", elem=1)" });
    }
    const byte *inbuf = img.pixels_view().data();
    std::vector<byte> outbuf(*buf_size);
    for (size_t y = 0; y < nny; y++) {
      for (size_t x = 0; x < nnx; x++) {
        for (size_t k = 0; k < nc; k++) { outbuf[nc * (y * nnx + x) + k] = inbuf[nc * (ylut[y] * nx + xlut[x]) + k]; }
      }
    }
    img.set_pixels(std::move(outbuf), nnx, nny, nc, bps);
  } else if (bps == 16) {
    const auto buf_size = checked_buf_size(nnx, nny, nc, 2);
    if (!buf_size) {
      return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
        "Pixel buffer size overflow (dimensions=" + std::to_string(nnx) + "x" + std::to_string(nny)
          + ", channels=" + std::to_string(nc) + ", elem=2)" });
    }
    const word *inbuf = reinterpret_cast<const word *>(img.pixels_view().data());
    std::vector<byte> outbuf_v(*buf_size);
    word *outbuf = reinterpret_cast<word *>(outbuf_v.data());
    for (size_t y = 0; y < nny; y++) {
      for (size_t x = 0; x < nnx; x++) {
        for (size_t k = 0; k < nc; k++) { outbuf[nc * (y * nnx + x) + k] = inbuf[nc * (ylut[y] * nx + xlut[x]) + k]; }
      }
    }
    img.set_pixels(std::move(outbuf_v), nnx, nny, nc, bps);
  } else {
    return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
      "Cannot scale image with unsupported bits/sample (" + std::to_string(bps) + "), only 8 and 16 are supported" });
  }

  return {};
}

//============================================================================

Result<void> scaleMedium(SipiImage &img, size_t nnx, size_t nny)
{
  SIPI_ZONE_N("SipiImage::scaleMedium");
  const size_t nx = img.getNx();
  const size_t ny = img.getNy();
  // A requested axis of one sample or fewer has no resampling to do; the
  // requested size is a malformed IIIF size parameter.
  if (nnx <= 1 || nny <= 1) {
    return std::unexpected(SipiValueError{ ErrorCode::kInvalidRequestParameter,
      "scaleMedium: unsupported target dimensions (src=" + std::to_string(nx) + "x" + std::to_string(ny)
        + ", dst=" + std::to_string(nnx) + "x" + std::to_string(nny) + "): each axis must be at least 2 samples" });
  }
  // A source axis of one sample or fewer has no resampling to do; the source
  // dimensions are a property of the decoded file, not the request.
  if (nx <= 1 || ny <= 1) {
    return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
      "scaleMedium: unsupported source dimensions (src=" + std::to_string(nx) + "x" + std::to_string(ny)
        + ", dst=" + std::to_string(nnx) + "x" + std::to_string(nny) + "): each axis must be at least 2 samples" });
  }

  auto xlut = std::make_unique<double[]>(nnx);
  auto ylut = std::make_unique<double[]>(nny);

  for (size_t i = 0; i < nnx; i++) { xlut[i] = (double)(i * (nx - 1)) / (double)(nnx - 1); }
  for (size_t j = 0; j < nny; j++) { ylut[j] = (double)(j * (ny - 1)) / (double)(nny - 1); }

  const size_t nc = img.getNc();
  const size_t bps = img.getBps();

  if (bps == 8) {
    const auto buf_size = checked_buf_size(nnx, nny, nc, 1);
    if (!buf_size) {
      return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
        "Pixel buffer size overflow (dimensions=" + std::to_string(nnx) + "x" + std::to_string(nny)
          + ", channels=" + std::to_string(nc) + ", elem=1)" });
    }
    const byte *inbuf = img.pixels_view().data();
    std::vector<byte> outbuf(*buf_size);
    double rx, ry;

    for (size_t j = 0; j < nny; j++) {
      ry = ylut[j];
      for (size_t i = 0; i < nnx; i++) {
        rx = xlut[i];
        for (size_t k = 0; k < nc; k++) { outbuf[nc * (j * nnx + i) + k] = bilinn(inbuf, nx, ny, rx, ry, k, nc); }
      }
    }

    img.set_pixels(std::move(outbuf), nnx, nny, nc, bps);
  } else if (bps == 16) {
    const auto buf_size = checked_buf_size(nnx, nny, nc, 2);
    if (!buf_size) {
      return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
        "Pixel buffer size overflow (dimensions=" + std::to_string(nnx) + "x" + std::to_string(nny)
          + ", channels=" + std::to_string(nc) + ", elem=2)" });
    }
    const word *inbuf = reinterpret_cast<const word *>(img.pixels_view().data());
    std::vector<byte> outbuf_v(*buf_size);
    word *outbuf = reinterpret_cast<word *>(outbuf_v.data());
    double rx, ry;

    for (size_t j = 0; j < nny; j++) {
      ry = ylut[j];
      for (size_t i = 0; i < nnx; i++) {
        rx = xlut[i];
        for (size_t k = 0; k < nc; k++) { outbuf[nc * (j * nnx + i) + k] = bilinn(inbuf, nx, ny, rx, ry, k, nc); }
      }
    }

    img.set_pixels(std::move(outbuf_v), nnx, nny, nc, bps);
  } else {
    return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
      "Cannot scale image with unsupported bits/sample (" + std::to_string(bps) + "), only 8 and 16 are supported" });
  }

  return {};
}

//============================================================================

Result<void> scale(SipiImage &img, size_t nnx, size_t nny)
{
  SIPI_ZONE_N("SipiImage::scale");
  const size_t nx = img.getNx();
  const size_t ny = img.getNy();
  // A requested axis of one sample or fewer has no resampling to do; the
  // requested size is a malformed IIIF size parameter.
  if (nnx <= 1 || nny <= 1) {
    return std::unexpected(SipiValueError{ ErrorCode::kInvalidRequestParameter,
      "scale: unsupported target dimensions (src=" + std::to_string(nx) + "x" + std::to_string(ny) + ", dst="
        + std::to_string(nnx) + "x" + std::to_string(nny) + "): each axis must be at least 2 samples" });
  }
  // A source axis of one sample or fewer has no resampling to do; the source
  // dimensions are a property of the decoded file, not the request.
  if (nx <= 1 || ny <= 1) {
    return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
      "scale: unsupported source dimensions (src=" + std::to_string(nx) + "x" + std::to_string(ny) + ", dst="
        + std::to_string(nnx) + "x" + std::to_string(nny) + "): each axis must be at least 2 samples" });
  }

  const size_t nc = img.getNc();
  const size_t bps = img.getBps();

  // Separable resample: one pass per axis, work proportional to the output size
  // rather than the source. Area averaging on a shrinking axis anti-aliases the
  // downscale; linear interpolation enlarges. The SIMD kernel lives in
  // resample.cc (Highway); the weights carry the fixed-point contract.
  const AxisWeights wx = build_axis_weights(nx, nnx);
  const AxisWeights wy = build_axis_weights(ny, nny);

  const auto buf_size = checked_buf_size(nnx, nny, nc, bps == 16 ? 2 : 1);
  if (!buf_size) {
    return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
      "Pixel buffer size overflow (dimensions=" + std::to_string(nnx) + "x" + std::to_string(nny)
        + ", channels=" + std::to_string(nc) + ", elem=" + std::to_string(bps == 16 ? 2 : 1) + ")" });
  }
  std::vector<byte> out(*buf_size);
  if (bps == 8) {
    resample_separable_u8(img.pixels_view().data(), nx, ny, nc, nnx, nny, wx.offset.data(), wx.idx.data(),
      wx.wt.data(), wy.offset.data(), wy.idx.data(), wy.wt.data(), out.data());
  } else if (bps == 16) {
    resample_separable_u16(reinterpret_cast<const word *>(img.pixels_view().data()), nx, ny, nc, nnx, nny,
      wx.offset.data(), wx.idx.data(), wx.wt.data(), wy.offset.data(), wy.idx.data(), wy.wt.data(),
      reinterpret_cast<word *>(out.data()));
  } else {
    return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
      "Cannot scale image with unsupported bits/sample (" + std::to_string(bps) + "), only 8 and 16 are supported" });
  }
  img.set_pixels(std::move(out), nnx, nny, nc, bps);
  return {};
}

//============================================================================

Result<void> rotate(SipiImage &img, float angle, bool mirror)
{
  SIPI_ZONE_N("SipiImage::rotate");
  const size_t nx = img.getNx();
  const size_t ny = img.getNy();
  const size_t nc = img.getNc();
  const size_t bps = img.getBps();

  if (mirror) {
    if (bps == 8) {
      const byte *inbuf = img.pixels_view().data();
      const auto buf_size = checked_buf_size(nx, ny, nc, 1);
      if (!buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
            + ", channels=" + std::to_string(nc) + ", elem=1)" });
      }
      std::vector<byte> outbuf(*buf_size);
      for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
          for (size_t k = 0; k < nc; k++) { outbuf[nc * (j * nx + i) + k] = inbuf[nc * (j * nx + (nx - i - 1)) + k]; }
        }
      }

      img.set_pixels(std::move(outbuf), nx, ny, nc, bps);
    } else if (bps == 16) {
      const word *inbuf = reinterpret_cast<const word *>(img.pixels_view().data());
      const auto buf_size = checked_buf_size(nx, ny, nc, 2);
      if (!buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
            + ", channels=" + std::to_string(nc) + ", elem=2)" });
      }
      std::vector<byte> outbuf_v(*buf_size);
      word *outbuf = reinterpret_cast<word *>(outbuf_v.data());

      for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
          for (size_t k = 0; k < nc; k++) { outbuf[nc * (j * nx + i) + k] = inbuf[nc * (j * nx + (nx - i - 1)) + k]; }
        }
      }

      img.set_pixels(std::move(outbuf_v), nx, ny, nc, bps);
    } else {
      return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
        "Cannot rotate image with unsupported bits/sample (" + std::to_string(bps)
          + "), only 8 and 16 are supported" });
    }
  }

  while (angle < 0.) angle += 360.;
  while (angle >= 360.) angle -= 360.;

  if (angle == 0.) { return {}; }

  if (angle == 90.) {
    //
    // abcdef     mga
    // ghijkl ==> nhb
    // mnopqr     oic
    //            pjd
    //            qke
    //            rlf
    //
    size_t nnx = ny;
    size_t nny = nx;

    if (bps == 8) {
      const byte *inbuf = img.pixels_view().data();
      const auto buf_size = checked_buf_size(nx, ny, nc, 1);
      if (!buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
            + ", channels=" + std::to_string(nc) + ", elem=1)" });
      }
      std::vector<byte> outbuf(*buf_size);

      for (size_t j = 0; j < nny; j++) {
        for (size_t i = 0; i < nnx; i++) {
          for (size_t k = 0; k < nc; k++) { outbuf[nc * (j * nnx + i) + k] = inbuf[nc * ((ny - i - 1) * nx + j) + k]; }
        }
      }

      img.set_pixels(std::move(outbuf), nnx, nny, nc, bps);
    } else if (bps == 16) {
      const word *inbuf = reinterpret_cast<const word *>(img.pixels_view().data());
      const auto buf_size = checked_buf_size(nx, ny, nc, 2);
      if (!buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
            + ", channels=" + std::to_string(nc) + ", elem=2)" });
      }
      std::vector<byte> outbuf_v(*buf_size);
      word *outbuf = reinterpret_cast<word *>(outbuf_v.data());

      for (size_t j = 0; j < nny; j++) {
        for (size_t i = 0; i < nnx; i++) {
          for (size_t k = 0; k < nc; k++) { outbuf[nc * (j * nnx + i) + k] = inbuf[nc * ((ny - i - 1) * nx + j) + k]; }
        }
      }

      img.set_pixels(std::move(outbuf_v), nnx, nny, nc, bps);
    }
  } else if (angle == 180.) {
    //
    // abcdef     rqponm
    // ghijkl ==> lkjihg
    // mnopqr     fedcba
    //
    size_t nnx = nx;
    size_t nny = ny;
    if (bps == 8) {
      const byte *inbuf = img.pixels_view().data();
      const auto buf_size = checked_buf_size(nx, ny, nc, 1);
      if (!buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
            + ", channels=" + std::to_string(nc) + ", elem=1)" });
      }
      std::vector<byte> outbuf(*buf_size);

      for (size_t j = 0; j < nny; j++) {
        for (size_t i = 0; i < nnx; i++) {
          for (size_t k = 0; k < nc; k++) {
            outbuf[nc * (j * nnx + i) + k] = inbuf[nc * ((ny - j - 1) * nx + (nx - i - 1)) + k];
          }
        }
      }

      img.set_pixels(std::move(outbuf), nnx, nny, nc, bps);
    } else if (bps == 16) {
      const word *inbuf = reinterpret_cast<const word *>(img.pixels_view().data());
      const auto buf_size = checked_buf_size(nx, ny, nc, 2);
      if (!buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
            + ", channels=" + std::to_string(nc) + ", elem=2)" });
      }
      std::vector<byte> outbuf_v(*buf_size);
      word *outbuf = reinterpret_cast<word *>(outbuf_v.data());

      for (size_t j = 0; j < nny; j++) {
        for (size_t i = 0; i < nnx; i++) {
          for (size_t k = 0; k < nc; k++) {
            outbuf[nc * (j * nnx + i) + k] = inbuf[nc * ((ny - j - 1) * nx + (nx - i - 1)) + k];
          }
        }
      }

      img.set_pixels(std::move(outbuf_v), nnx, nny, nc, bps);
    }
  } else if (angle == 270.) {
    //
    // abcdef     flr
    // ghijkl ==> ekq
    // mnopqr     djp
    //            cio
    //            bhn
    //            agm
    //
    size_t nnx = ny;
    size_t nny = nx;

    if (bps == 8) {
      const byte *inbuf = img.pixels_view().data();
      const auto buf_size = checked_buf_size(nx, ny, nc, 1);
      if (!buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
            + ", channels=" + std::to_string(nc) + ", elem=1)" });
      }
      std::vector<byte> outbuf(*buf_size);
      for (size_t j = 0; j < nny; j++) {
        for (size_t i = 0; i < nnx; i++) {
          for (size_t k = 0; k < nc; k++) { outbuf[nc * (j * nnx + i) + k] = inbuf[nc * (i * nx + (nx - j - 1)) + k]; }
        }
      }

      img.set_pixels(std::move(outbuf), nnx, nny, nc, bps);
    } else if (bps == 16) {
      const word *inbuf = reinterpret_cast<const word *>(img.pixels_view().data());
      const auto buf_size = checked_buf_size(nx, ny, nc, 2);
      if (!buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
            + ", channels=" + std::to_string(nc) + ", elem=2)" });
      }
      std::vector<byte> outbuf_v(*buf_size);
      word *outbuf = reinterpret_cast<word *>(outbuf_v.data());
      for (size_t j = 0; j < nny; j++) {
        for (size_t i = 0; i < nnx; i++) {
          for (size_t k = 0; k < nc; k++) { outbuf[nc * (j * nnx + i) + k] = inbuf[nc * (i * nx + (nx - j - 1)) + k]; }
        }
      }
      img.set_pixels(std::move(outbuf_v), nnx, nny, nc, bps);
    }
  } else {
    // all other angles
    double phi = M_PI * angle / 180.0;
    double ptx = static_cast<double>(nx) / 2. - .5;
    double pty = static_cast<double>(ny) / 2. - .5;

    double si = sin(-phi);
    double co = cos(-phi);

    size_t nnx;
    size_t nny;

    if ((angle > 0.) && (angle < 90.)) {
      nnx = floor((double)nx * cos(phi) + (double)ny * sin(phi) + .5);
      nny = floor((double)nx * sin(phi) + (double)ny * cos(phi) + .5);
    } else if ((angle > 90.) && (angle < 180.)) {
      nnx = floor(-((double)nx) * cos(phi) + (double)ny * sin(phi) + .5);
      nny = floor((double)nx * sin(phi) - (double)ny * cos(phi) + .5);
    } else if ((angle > 180.) && (angle < 270.)) {
      nnx = floor(-((double)nx) * cos(phi) - (double)ny * sin(phi) + .5);
      nny = floor(-((double)nx) * sin(phi) - (double)ny * cos(phi) + .5);
    } else {
      nnx = floor((double)nx * cos(phi) - (double)ny * sin(phi) + .5);
      nny = floor(-((double)nx) * sin(phi) + (double)ny * cos(phi) + .5);
    }

    double pptx = ptx * (double)nnx / (double)nx;
    double ppty = pty * (double)nny / (double)ny;

    if (bps == 8) {
      const byte *inbuf = img.pixels_view().data();
      const auto buf_size = checked_buf_size(nnx, nny, nc, 1);
      if (!buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nnx) + "x" + std::to_string(nny)
            + ", channels=" + std::to_string(nc) + ", elem=1)" });
      }
      std::vector<byte> outbuf(*buf_size);
      byte bg = 0;

      for (size_t j = 0; j < nny; j++) {
        for (size_t i = 0; i < nnx; i++) {
          double rx = ((double)i - pptx) * co - ((double)j - ppty) * si + ptx;
          double ry = ((double)i - pptx) * si + ((double)j - ppty) * co + pty;

          if ((rx < 0.0) || (rx >= (double)(nx - 1)) || (ry < 0.0) || (ry >= (double)(ny - 1))) {
            for (size_t k = 0; k < nc; k++) { outbuf[nc * (j * nnx + i) + k] = bg; }
          } else {
            for (size_t k = 0; k < nc; k++) { outbuf[nc * (j * nnx + i) + k] = bilinn(inbuf, nx, ny, rx, ry, k, nc); }
          }
        }
      }

      img.set_pixels(std::move(outbuf), nnx, nny, nc, bps);
    } else if (bps == 16) {
      const word *inbuf = reinterpret_cast<const word *>(img.pixels_view().data());
      const auto buf_size = checked_buf_size(nnx, nny, nc, 2);
      if (!buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nnx) + "x" + std::to_string(nny)
            + ", channels=" + std::to_string(nc) + ", elem=2)" });
      }
      std::vector<byte> outbuf_v(*buf_size);
      word *outbuf = reinterpret_cast<word *>(outbuf_v.data());
      word bg = 0;

      for (size_t j = 0; j < nny; j++) {
        for (size_t i = 0; i < nnx; i++) {
          double rx = ((double)i - pptx) * co - ((double)j - ppty) * si + ptx;
          double ry = ((double)i - pptx) * si + ((double)j - ppty) * co + pty;

          if ((rx < 0.0) || (rx >= (double)(nx - 1)) || (ry < 0.0) || (ry >= (double)(ny - 1))) {
            for (size_t k = 0; k < nc; k++) { outbuf[nc * (j * nnx + i) + k] = bg; }
          } else {
            for (size_t k = 0; k < nc; k++) { outbuf[nc * (j * nnx + i) + k] = bilinn(inbuf, nx, ny, rx, ry, k, nc); }
          }
        }
      }

      img.set_pixels(std::move(outbuf_v), nnx, nny, nc, bps);
    }
  }
  return {};
}

//============================================================================

Result<void> set_topleft(SipiImage &img)
{
  Result<void> r;
  switch (img.getOrientation()) {
  case TOPLEFT:// 1
    return {};
  case TOPRIGHT:// 2
    r = rotate(img, 0., true);
    break;
  case BOTRIGHT:// 3
    r = rotate(img, 180., false);
    break;
  case BOTLEFT:// 4
    r = rotate(img, 180., true);
    break;
  case LEFTTOP:// 5
    r = rotate(img, 270., true);
    break;
  case RIGHTTOP:// 6
    r = rotate(img, 90., false);
    break;
  case RIGHTBOT:// 7
    r = rotate(img, 90., true);
    break;
  case LEFTBOT:// 8
    r = rotate(img, 270., false);
    break;
  default:;// nothing to do...
  }
  if (!r) { return r; }
  img.setOrientation(TOPLEFT);
  if (const auto exif = img.getExif(); exif != nullptr) {
    exif->addKeyVal("Exif.Image.Orientation", static_cast<unsigned short>(TOPLEFT));
  }
  return {};
}

}// namespace processing
}// namespace Sipi
