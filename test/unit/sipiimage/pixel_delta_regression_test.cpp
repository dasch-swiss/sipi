/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Regression test — `SipiImage::maxPixelDelta` (the metric behind the
 * `sipi compare` command). The original `run_compare` loop computed the
 * per-channel difference as `size_t dv = img1.getPixel(...) - img2.getPixel(...)`,
 * an unsigned subtraction that underflows to a huge value whenever the
 * second image's sample is larger — corrupting the reported max and avg.
 * These tests assert the true *absolute* delta is reported for diffs in
 * BOTH directions (some pixels brighter, some darker), so a regression to
 * the unsigned form would produce an enormous max and fail loudly.
 */

#include <gtest/gtest.h>

#include "../../../src/SipiImage.hpp"

namespace {

using Sipi::PhotometricInterpretation;
using Sipi::PixelDelta;
using Sipi::SipiImage;

// `setPixel`/`getPixel` and the pixel store read by `maxPixelDelta` are
// both row-major `y * nx + x`, so the (max_x, max_y) reported for a value
// written via setPixel(x, y) is exactly (x, y). The non-square case is
// covered by pixel_accessor_regression_test; this square fixture passes with
// or without the fix only because its maximum-delta pixel is on the diagonal
// (2, 2), where the two index formulae coincide.

TEST(MaxPixelDelta, AbsoluteDeltaBothDirections)
{
  constexpr size_t N = 4;
  SipiImage img1(N, N, 1, 8, PhotometricInterpretation::MINISBLACK);
  SipiImage img2(N, N, 1, 8, PhotometricInterpretation::MINISBLACK);

  // Constructor does not zero-initialize the buffer — fill every cell.
  for (size_t y = 0; y < N; ++y) {
    for (size_t x = 0; x < N; ++x) {
      img1.setPixel(x, y, 0, 100);
      img2.setPixel(x, y, 0, 100);
    }
  }

  // img2 brighter (the unsigned-underflow trigger): |Δ| = 190.
  img1.setPixel(1, 0, 0, 10);
  img2.setPixel(1, 0, 0, 200);
  // img1 brighter: |Δ| = 150.
  img1.setPixel(0, 1, 0, 200);
  img2.setPixel(0, 1, 0, 50);
  // Maximum, on the diagonal, img2 brighter: |Δ| = 250.
  img1.setPixel(2, 2, 0, 5);
  img2.setPixel(2, 2, 0, 255);

  const std::optional<PixelDelta> delta = img1.maxPixelDelta(img2);
  ASSERT_TRUE(delta.has_value());

  // True max |Δ| is 250 — a regressed unsigned subtraction would report a
  // value near SIZE_MAX for the 5-vs-255 pixel.
  EXPECT_EQ(delta->max_abs, 250);
  EXPECT_EQ(delta->max_x, 2u);
  EXPECT_EQ(delta->max_y, 2u);

  // Mean |Δ| = (190 + 150 + 250) / 16 samples = 36.875.
  EXPECT_DOUBLE_EQ(delta->mean_abs, 590.0 / 16.0);
}

TEST(MaxPixelDelta, SixteenBitBrighterSecondImage)
{
  SipiImage img1(2, 2, 1, 16, PhotometricInterpretation::MINISBLACK);
  SipiImage img2(2, 2, 1, 16, PhotometricInterpretation::MINISBLACK);

  for (size_t y = 0; y < 2; ++y) {
    for (size_t x = 0; x < 2; ++x) {
      img1.setPixel(x, y, 0, 1000);
      img2.setPixel(x, y, 0, 1000);
    }
  }

  // 16-bit, img2 far brighter: |Δ| = 59000. The unsigned form underflows
  // even more catastrophically here.
  img1.setPixel(0, 0, 0, 1000);
  img2.setPixel(0, 0, 0, 60000);

  const std::optional<PixelDelta> delta = img1.maxPixelDelta(img2);
  ASSERT_TRUE(delta.has_value());
  EXPECT_EQ(delta->max_abs, 59000);
  EXPECT_DOUBLE_EQ(delta->mean_abs, 59000.0 / 4.0);
}

TEST(MaxPixelDelta, IdenticalImagesReportZero)
{
  SipiImage img1(3, 3, 1, 8, PhotometricInterpretation::MINISBLACK);
  SipiImage img2(3, 3, 1, 8, PhotometricInterpretation::MINISBLACK);

  for (size_t y = 0; y < 3; ++y) {
    for (size_t x = 0; x < 3; ++x) {
      img1.setPixel(x, y, 0, 42);
      img2.setPixel(x, y, 0, 42);
    }
  }

  const std::optional<PixelDelta> delta = img1.maxPixelDelta(img2);
  ASSERT_TRUE(delta.has_value());
  EXPECT_EQ(delta->max_abs, 0);
  EXPECT_DOUBLE_EQ(delta->mean_abs, 0.0);
}

TEST(MaxPixelDelta, IncomparableDimensionsReturnsNullopt)
{
  SipiImage img1(4, 4, 1, 8, PhotometricInterpretation::MINISBLACK);
  SipiImage img2(2, 2, 1, 8, PhotometricInterpretation::MINISBLACK);

  EXPECT_FALSE(img1.maxPixelDelta(img2).has_value());
}

}// namespace
