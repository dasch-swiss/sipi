/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Characterization of SipiImage::scale() — the separable two-pass resampler
 * (area averaging on a shrinking axis, 2-tap linear interpolation on an
 * enlarging axis, fixed-point integer accumulation). These fixed-input /
 * exact-output cases pin the resampler's output: integer accumulation is exact
 * and associative, so the same expected values hold across the scalar reference
 * and every SIMD target. Every expected value is a clean integer computed by
 * hand from the documented filter, far from any rounding boundary.
 */

#include <gtest/gtest.h>

#include <vector>

#include "../../../src/SipiImage.h"

namespace {

using Sipi::PhotometricInterpretation;
using Sipi::SipiImage;

// Build a single-channel image whose pixels are the given row-major values.
SipiImage make_gray(size_t nx, size_t ny, int bps, const std::vector<int> &values)
{
  SipiImage img(nx, ny, 1, bps, PhotometricInterpretation::MINISBLACK);
  for (size_t y = 0; y < ny; ++y) {
    for (size_t x = 0; x < nx; ++x) { img.setPixel(x, y, 0, values[y * nx + x]); }
  }
  return img;
}

void expect_gray(SipiImage &img, size_t nx, size_t ny, const std::vector<int> &expected)
{
  ASSERT_EQ(img.getNx(), nx);
  ASSERT_EQ(img.getNy(), ny);
  for (size_t y = 0; y < ny; ++y) {
    for (size_t x = 0; x < nx; ++x) {
      EXPECT_EQ(img.getPixel(x, y, 0), expected[y * nx + x]) << "at (" << x << "," << y << ")";
    }
  }
}

// 4x4 -> 2x2, ratio 2: each output averages a 2-wide box. Rows carry the
// horizontal ramp [0,40,80,120] -> [20,100]; the vertical pass over identical
// rows leaves both output rows equal.
TEST(ScaleResample, DownscaleAreaAverageHorizontalGradient)
{
  auto img = make_gray(4, 4, 8, { 0, 40, 80, 120, 0, 40, 80, 120, 0, 40, 80, 120, 0, 40, 80, 120 });
  ASSERT_TRUE(img.scale(2, 2));
  expect_gray(img, 2, 2, { 20, 100, 20, 100 });
}

// 4x4 -> 2x2 with a vertical ramp (rows 0/40/80/120, constant across x): the
// horizontal pass is a no-op on constant rows, the vertical pass averages
// row pairs -> [20;100].
TEST(ScaleResample, DownscaleAreaAverageVerticalGradient)
{
  auto img = make_gray(4, 4, 8, { 0, 0, 0, 0, 40, 40, 40, 40, 80, 80, 80, 80, 120, 120, 120, 120 });
  ASSERT_TRUE(img.scale(2, 2));
  expect_gray(img, 2, 2, { 20, 20, 100, 100 });
}

// 2x2 -> 4x4, 2-tap linear interpolation. Sample positions i*(src-1)/(dst-1)
// give fractions 0, 1/3, 2/3, 1 on each axis; the separable passes produce a
// symmetric bilinear ramp.
TEST(ScaleResample, EnlargeLinearInterpolation)
{
  auto img = make_gray(2, 2, 8, { 0, 90, 90, 180 });
  ASSERT_TRUE(img.scale(4, 4));
  expect_gray(img,
    4,
    4,
    {
      0, 30, 60, 90,       //
      30, 60, 90, 120,     //
      60, 90, 120, 150,    //
      90, 120, 150, 180    //
    });
}

// 4x4 -> 3x3, non-integer ratio 4/3. Horizontal ramp [0,40,80,120] resolves to
// weights 0.75/0.25, 0.5/0.5, 0.25/0.75 -> [10,60,110]; the vertical pass over
// identical rows preserves it.
TEST(ScaleResample, DownscaleNonIntegerRatio)
{
  auto img = make_gray(4, 4, 8, { 0, 40, 80, 120, 0, 40, 80, 120, 0, 40, 80, 120, 0, 40, 80, 120 });
  ASSERT_TRUE(img.scale(3, 3));
  expect_gray(img, 3, 3, { 10, 60, 110, 10, 60, 110, 10, 60, 110 });
}

// The 16-bit branch of the resampler (word samples) area-averages identically.
TEST(ScaleResample, Downscale16Bit)
{
  auto img = make_gray(4, 4, 16,
    { 0, 4000, 8000, 12000, 0, 4000, 8000, 12000, 0, 4000, 8000, 12000, 0, 4000, 8000, 12000 });
  ASSERT_TRUE(img.scale(2, 2));
  expect_gray(img, 2, 2, { 2000, 10000, 2000, 10000 });
}

}// namespace
