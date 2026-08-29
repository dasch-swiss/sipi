/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "processing.h"

#include "image/SipiImageError.h"
#include "util/checked_arith.h"
#include "util/Global.h"

namespace Sipi {

namespace {

// Every pixel-buffer allocation in this file funnels through here: reject
// (throw) the moment `nx * ny * nc * elem` overflows `size_t`, rather than
// letting a wrapped size feed a too-small allocation that a later copy then
// overruns.
size_t checked_buf_size_or_throw(size_t nx_, size_t ny_, size_t nc_, size_t elem_)
{
  if (const auto sz = checked_buf_size(nx_, ny_, nc_, elem_)) { return *sz; }
  throw SipiImageError("Pixel buffer size overflow (dimensions=" + std::to_string(nx_) + "x" + std::to_string(ny_)
                        + ", channels=" + std::to_string(nc_) + ", elem=" + std::to_string(elem_) + ")");
}

}// namespace

namespace processing {

Result<void> add_watermark(SipiImage &img, const std::string &wmfilename)
{
  int wm_nx, wm_ny, wm_nc;
  std::vector<unsigned char> wm = read_watermark(wmfilename, wm_nx, wm_ny, wm_nc);
  if (wm.empty()) {
    return std::unexpected(SipiValueError{ ErrorCode::kDecodeFailed, "Cannot read watermark file " + wmfilename });
  }
  byte *wmbuf = wm.data();

  const size_t nx = img.getNx();
  const size_t ny = img.getNy();
  const size_t nc = img.getNc();
  const size_t bps = img.getBps();

  // scaling is calculated with the middle point as point of origin
  double wm_scale = ((double)wm_nx / wm_ny > (double)nx / ny) ? (double)wm_nx / nx : (double)wm_ny / ny;

  // blending alpha coefficient (multiplies with alpha channel if there is one)
  double wm_strength = 0.8;

  if (bps == 8) {
    auto buf = img.pixels_writable();

    for (size_t j = 0; j < ny; j++) {
      for (size_t i = 0; i < nx; i++) {
        double wm_i = (i - nx / 2.) * wm_scale + wm_nx / 2.;
        double wm_j = (j - ny / 2.) * wm_scale + wm_ny / 2.;

        for (size_t k = 0; k < nc; k++) {
          if (!(abs(wm_i - wm_nx / 2.) < wm_nx / 2. && abs(wm_j - wm_ny / 2.) < wm_ny / 2.)) continue;

          // Map channel index into watermark's channel range to prevent OOB
          // when image has more channels than watermark (e.g., 3-ch CIELab + 1-ch grayscale wm)
          size_t wm_k = k % static_cast<size_t>(wm_nc);

          double wm_alpha = wm_nc == 4 ? bilinn(wmbuf, wm_nx, wm_ny, wm_i, wm_j, 3, wm_nc) : 1;
          double wm_alpha_weak = wm_alpha / 255.0 * wm_strength;

          double wm_color = bilinn(wmbuf, wm_nx, wm_ny, wm_i, wm_j, wm_k, wm_nc) / 255.0;
          double color = (buf[nc * (j * nx + i) + k] / 255.);
          double blend = color * (1.0 - wm_alpha_weak) + wm_color * wm_alpha_weak;
          buf[nc * (j * nx + i) + k] = std::clamp(blend * 255., 0., 255.);
        }
      }
    }
  } else if (bps == 16) {
    // 16bps support was never really finished, left unimplemented
  }

  return {};
}

/*==========================================================================*/

std::optional<double> compare(const SipiImage &lhs, const SipiImage &rhs)
{
  if ((lhs.getNx() != rhs.getNx()) || (lhs.getNy() != rhs.getNy()) || (lhs.getNc() != rhs.getNc())
      || (lhs.getBps() != rhs.getBps()) || (lhs.getPhoto() != rhs.getPhoto())) {
    return {};
  }

  const size_t nx = lhs.getNx();
  const size_t ny = lhs.getNy();
  const size_t nc = lhs.getNc();
  const size_t bps = lhs.getBps();

  double diff = 0;
  double niters = 0;

  switch (bps) {
  case 8: {
    const byte *ltmp1 = lhs.pixels_view().data();
    const byte *ltmp2 = rhs.pixels_view().data();
    for (size_t j = 0; j < ny; j++) {
      for (size_t i = 0; i < nx; i++) {
        for (size_t k = 0; k < nc; k++) {
          niters++;
          diff += abs(ltmp1[nc * (j * nx + i) + k] - ltmp2[nc * (j * nx + i) + k]) / 255.;
        }
      }
    }
    break;
  }
  case 16:
    return 1;
  }

  return diff / niters;
}

/*==========================================================================*/

std::optional<PixelDelta> maxPixelDelta(const SipiImage &lhs, const SipiImage &rhs)
{
  if ((lhs.getNx() != rhs.getNx()) || (lhs.getNy() != rhs.getNy()) || (lhs.getNc() != rhs.getNc())
      || (lhs.getBps() != rhs.getBps()) || (lhs.getPhoto() != rhs.getPhoto())) {
    return {};
  }

  const size_t nx = lhs.getNx();
  const size_t ny = lhs.getNy();
  const size_t nc = lhs.getNc();
  const size_t bps = lhs.getBps();

  // Read the raw pixel store directly in row-major order (`y * nx + x`),
  // matching operator-=, the format handlers, and getPixel/setPixel.
  double sum_abs = 0.;
  int max_abs = 0;
  size_t max_x = 0, max_y = 0;

  // Absolute per-channel difference over a signed widening of the two
  // samples: a plain `s1 - s2` is non-negative-typed at the call site
  // (getPixel returns int but the historical compare loop stored it in a
  // size_t), which underflows for s2 > s1 and corrupts the maximum.
  auto accumulate = [&](size_t x, size_t y, int s1, int s2) {
    const int signed_diff = s1 - s2;
    const int dv = signed_diff < 0 ? -signed_diff : signed_diff;
    if (dv > max_abs) {
      max_abs = dv;
      max_x = x;
      max_y = y;
    }
    sum_abs += dv;
  };

  switch (bps) {
  case 8: {
    const byte *ltmp = lhs.pixels_view().data();
    const byte *rtmp = rhs.pixels_view().data();
    for (size_t y = 0; y < ny; y++) {
      for (size_t x = 0; x < nx; x++) {
        for (size_t c = 0; c < nc; c++) {
          const size_t idx = nc * (y * nx + x) + c;
          accumulate(x, y, ltmp[idx], rtmp[idx]);
        }
      }
    }
    break;
  }
  case 16: {
    const word *ltmp = reinterpret_cast<const word *>(lhs.pixels_view().data());
    const word *rtmp = reinterpret_cast<const word *>(rhs.pixels_view().data());
    for (size_t y = 0; y < ny; y++) {
      for (size_t x = 0; x < nx; x++) {
        for (size_t c = 0; c < nc; c++) {
          const size_t idx = nc * (y * nx + x) + c;
          accumulate(x, y, ltmp[idx], rtmp[idx]);
        }
      }
    }
    break;
  }
  default:
    return {};
  }

  const double mean_abs = sum_abs / static_cast<double>(nx * ny * nc);
  return PixelDelta{ .mean_abs = mean_abs, .max_abs = max_abs, .max_x = max_x, .max_y = max_y };
}

}// namespace processing

/*==========================================================================*/

bool operator==(const SipiImage &lhs, const SipiImage &rhs)
{
  if ((lhs.getNx() != rhs.getNx()) || (lhs.getNy() != rhs.getNy()) || (lhs.getNc() != rhs.getNc())
      || (lhs.getBps() != rhs.getBps()) || (lhs.getPhoto() != rhs.getPhoto())) {
    return false;
  }

  const size_t nx = lhs.getNx();
  const size_t ny = lhs.getNy();
  const size_t nc = lhs.getNc();

  long long n_differences = 0;

  switch (lhs.getBps()) {
  case 8: {
    const byte *ltmp1 = lhs.pixels_view().data();
    const byte *ltmp2 = rhs.pixels_view().data();
    for (size_t j = 0; j < ny; j++) {
      for (size_t i = 0; i < nx; i++) {
        for (size_t k = 0; k < nc; k++) {
          if (ltmp1[nc * (j * nx + i) + k] != ltmp2[nc * (j * nx + i) + k]) { n_differences++; }
        }
      }
    }
    break;
  }
  case 16: {
    const word *ltmp1 = reinterpret_cast<const word *>(lhs.pixels_view().data());
    const word *ltmp2 = reinterpret_cast<const word *>(rhs.pixels_view().data());
    for (size_t j = 0; j < ny; j++) {
      for (size_t i = 0; i < nx; i++) {
        for (size_t k = 0; k < nc; k++) {
          if (ltmp1[nc * (j * nx + i) + k] != ltmp2[nc * (j * nx + i) + k]) { n_differences++; }
        }
      }
    }
    break;
  }
  }

  return n_differences <= 0;
}

/*==========================================================================*/

SipiImage &operator-=(SipiImage &lhs, const SipiImage &rhs)
{
  if ((lhs.getNc() != rhs.getNc()) || (lhs.getBps() != rhs.getBps()) || (lhs.getPhoto() != rhs.getPhoto())) {
    std::stringstream ss;
    ss << "Image op: images not compatible" << std::endl;
    ss << "Image 1:  nc: " << lhs.getNc() << " bps: " << lhs.getBps() << " photo: " << shttps::as_integer(lhs.getPhoto())
       << std::endl;
    ss << "Image 2:  nc: " << rhs.getNc() << " bps: " << rhs.getBps() << " photo: " << shttps::as_integer(rhs.getPhoto())
       << std::endl;
    throw SipiImageError(ss.str());
  }

  const size_t nx = lhs.getNx();
  const size_t ny = lhs.getNy();
  const size_t nc = lhs.getNc();
  const size_t bps = lhs.getBps();

  // R15: RAII for temporary resources
  std::unique_ptr<SipiImage> new_rhs_guard;
  const SipiImage *rhs_ptr = &rhs;
  if ((nx != rhs.getNx()) || (ny != rhs.getNy())) {
    new_rhs_guard = std::make_unique<SipiImage>(rhs);
    if (auto scaled = processing::scale(*new_rhs_guard, nx, ny); !scaled) {
      throw SipiImageError(scaled.error().raw_message(), scaled.error().errnum(), scaled.error().location());
    }
    rhs_ptr = new_rhs_guard.get();
  }

  const size_t diffbuf_bytes = checked_buf_size_or_throw(nx, ny, nc, sizeof(int));
  auto diffbuf = std::make_unique<int[]>(diffbuf_bytes / sizeof(int));

  int *dbuf = diffbuf.get();

  switch (bps) {
  case 8: {
    byte *ltmp = lhs.pixels_writable().data();
    const byte *rtmp = rhs_ptr->pixels_view().data();

    for (size_t j = 0; j < ny; j++) {
      for (size_t i = 0; i < nx; i++) {
        for (size_t k = 0; k < nc; k++) {
          if (ltmp[nc * (j * nx + i) + k] != rtmp[nc * (j * nx + i) + k]) {
            dbuf[nc * (j * nx + i) + k] = ltmp[nc * (j * nx + i) + k] - rtmp[nc * (j * nx + i) + k];
          }
        }
      }
    }

    break;
  }

  case 16: {
    word *ltmp = reinterpret_cast<word *>(lhs.pixels_writable().data());
    const word *rtmp = reinterpret_cast<const word *>(rhs_ptr->pixels_view().data());

    for (size_t j = 0; j < ny; j++) {
      for (size_t i = 0; i < nx; i++) {
        for (size_t k = 0; k < nc; k++) {
          if (ltmp[nc * (j * nx + i) + k] != rtmp[nc * (j * nx + i) + k]) {
            dbuf[nc * (j * nx + i) + k] = ltmp[nc * (j * nx + i) + k] - rtmp[nc * (j * nx + i) + k];
          }
        }
      }
    }

    break;
  }

  default: {
    throw SipiImageError(
      "Unsupported bits/sample (" + std::to_string(bps) + ") for image diff operation, only 8 and 16 are supported");
  }
  }

  int min = INT_MAX;
  int max = INT_MIN;

  for (size_t j = 0; j < ny; j++) {
    for (size_t i = 0; i < nx; i++) {
      for (size_t k = 0; k < nc; k++) {
        if (dbuf[nc * (j * nx + i) + k] > max) max = dbuf[nc * (j * nx + i) + k];
        if (dbuf[nc * (j * nx + i) + k] < min) min = dbuf[nc * (j * nx + i) + k];
      }
    }
  }
  int maxmax = abs(min) > abs(max) ? abs(min) : abs(max);

  switch (bps) {
  case 8: {
    byte *ltmp = lhs.pixels_writable().data();

    for (size_t j = 0; j < ny; j++) {
      for (size_t i = 0; i < nx; i++) {
        for (size_t k = 0; k < nc; k++) {
          ltmp[nc * (j * nx + i) + k] = (byte)((dbuf[nc * (j * nx + i) + k] + maxmax) * UCHAR_MAX / (2 * maxmax));
        }
      }
    }

    break;
  }

  case 16: {
    word *ltmp = reinterpret_cast<word *>(lhs.pixels_writable().data());

    for (size_t j = 0; j < ny; j++) {
      for (size_t i = 0; i < nx; i++) {
        for (size_t k = 0; k < nc; k++) {
          ltmp[nc * (j * nx + i) + k] = (word)((dbuf[nc * (j * nx + i) + k] + maxmax) * USHRT_MAX / (2 * maxmax));
        }
      }
    }

    break;
  }

  default: {
    throw SipiImageError(
      "Unsupported bits/sample (" + std::to_string(bps) + ") for image diff operation, only 8 and 16 are supported");
  }
  }

  // RAII: diffbuf and new_rhs_guard auto-freed on scope exit
  return lhs;
}

/*==========================================================================*/

// R14: Return by value — compiler applies RVO/NRVO
SipiImage operator-(const SipiImage &lhs, const SipiImage &rhs)
{
  SipiImage result(lhs);
  result -= rhs;
  return result;
}

/*==========================================================================*/

SipiImage &operator+=(SipiImage &lhs, const SipiImage &rhs)
{
  if ((lhs.getNc() != rhs.getNc()) || (lhs.getBps() != rhs.getBps()) || (lhs.getPhoto() != rhs.getPhoto())) {
    std::stringstream ss;
    ss << "Image op: images not compatible" << std::endl;
    ss << "Image 1:  nc: " << lhs.getNc() << " bps: " << lhs.getBps() << " photo: " << shttps::as_integer(lhs.getPhoto())
       << std::endl;
    ss << "Image 2:  nc: " << rhs.getNc() << " bps: " << rhs.getBps() << " photo: " << shttps::as_integer(rhs.getPhoto())
       << std::endl;
    throw SipiImageError(ss.str());
  }

  const size_t nx = lhs.getNx();
  const size_t ny = lhs.getNy();
  const size_t nc = lhs.getNc();
  const size_t bps = lhs.getBps();

  // R15: RAII for temporary resources (also fixes new_rhs leak)
  std::unique_ptr<SipiImage> new_rhs_guard;
  const SipiImage *rhs_ptr = &rhs;
  if ((nx != rhs.getNx()) || (ny != rhs.getNy())) {
    new_rhs_guard = std::make_unique<SipiImage>(rhs);
    if (auto scaled = processing::scale(*new_rhs_guard, nx, ny); !scaled) {
      throw SipiImageError(scaled.error().raw_message(), scaled.error().errnum(), scaled.error().location());
    }
    rhs_ptr = new_rhs_guard.get();
  }

  const size_t diffbuf_bytes = checked_buf_size_or_throw(nx, ny, nc, sizeof(int));
  auto diffbuf = std::make_unique<int[]>(diffbuf_bytes / sizeof(int));
  int *dbuf = diffbuf.get();

  switch (bps) {
  case 8: {
    byte *ltmp = lhs.pixels_writable().data();
    const byte *rtmp = rhs_ptr->pixels_view().data();

    for (size_t j = 0; j < ny; j++) {
      for (size_t i = 0; i < nx; i++) {
        for (size_t k = 0; k < nc; k++) {
          if (ltmp[nc * (j * nx + i) + k] != rtmp[nc * (j * nx + i) + k]) {
            dbuf[nc * (j * nx + i) + k] = ltmp[nc * (j * nx + i) + k] + rtmp[nc * (j * nx + i) + k];
          }
        }
      }
    }
    break;
  }

  case 16: {
    word *ltmp = reinterpret_cast<word *>(lhs.pixels_writable().data());
    const word *rtmp = reinterpret_cast<const word *>(rhs_ptr->pixels_view().data());

    for (size_t j = 0; j < ny; j++) {
      for (size_t i = 0; i < nx; i++) {
        for (size_t k = 0; k < nc; k++) {
          if (ltmp[nc * (j * nx + i) + k] != rtmp[nc * (j * nx + i) + k]) {
            // BUG FIX: was subtraction (copy-paste from operator-=), should be addition
            dbuf[nc * (j * nx + i) + k] = ltmp[nc * (j * nx + i) + k] + rtmp[nc * (j * nx + i) + k];
          }
        }
      }
    }

    break;
  }

  default: {
    throw SipiImageError(
      "Unsupported bits/sample (" + std::to_string(bps) + ") for image add operation, only 8 and 16 are supported");
  }
  }

  int max = INT_MIN;

  for (size_t j = 0; j < ny; j++) {
    for (size_t i = 0; i < nx; i++) {
      for (size_t k = 0; k < nc; k++) {
        if (dbuf[nc * (j * nx + i) + k] > max) max = dbuf[nc * (j * nx + i) + k];
      }
    }
  }

  switch (bps) {
  case 8: {
    byte *ltmp = lhs.pixels_writable().data();

    for (size_t j = 0; j < ny; j++) {
      for (size_t i = 0; i < nx; i++) {
        for (size_t k = 0; k < nc; k++) {
          ltmp[nc * (j * nx + i) + k] = (byte)(dbuf[nc * (j * nx + i) + k] * UCHAR_MAX / max);
        }
      }
    }

    break;
  }

  case 16: {
    word *ltmp = reinterpret_cast<word *>(lhs.pixels_writable().data());

    for (size_t j = 0; j < ny; j++) {
      for (size_t i = 0; i < nx; i++) {
        for (size_t k = 0; k < nc; k++) {
          ltmp[nc * (j * nx + i) + k] = (word)(dbuf[nc * (j * nx + i) + k] * USHRT_MAX / max);
        }
      }
    }

    break;
  }

  default: {
    throw SipiImageError(
      "Unsupported bits/sample (" + std::to_string(bps) + ") for image add operation, only 8 and 16 are supported");
  }
  }

  // RAII: diffbuf and new_rhs_guard auto-freed on scope exit
  return lhs;
}

/*==========================================================================*/

// R14: Return by value — compiler applies RVO/NRVO
SipiImage operator+(const SipiImage &lhs, const SipiImage &rhs)
{
  SipiImage result(lhs);
  result += rhs;
  return result;
}

}// namespace Sipi
