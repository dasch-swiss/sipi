/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Regression test — `SipiImage::getPixel`/`setPixel` index the pixel buffer
 * row-major (`nc * (y * nx + x) + c`), matching the codec store written by
 * the format handlers and read by `operator-=`/`maxPixelDelta`.
 *
 * Before the fix the accessors indexed transposed (`x * nx + y`). For a
 * square image both formulae cover the same index set, so a setPixel ->
 * getPixel round-trip stayed self-consistent and hid the bug; it only
 * diverges for non-square images, where a transposed setPixel writes to a
 * different offset than the codec store reads. These tests therefore use
 * non-square images.
 */

#include <gtest/gtest.h>

#include <optional>

#include "../../../src/SipiImage.hpp"

namespace {

using Sipi::PhotometricInterpretation;
using Sipi::PixelDelta;
using Sipi::SipiImage;

// Pins setPixel to the row-major store the format handlers and maxPixelDelta
// use: maxPixelDelta reads the raw buffer row-major and reports store-order
// coordinates, so it is an oracle independent of the accessor under test.
// With a non-square image and a delta at an off-diagonal coordinate, the
// transposed (pre-fix) setPixel would land at — and be reported at — the
// transposed location (1, 2), failing the asserts below.
TEST(PixelAccessor, SetPixelLandsAtRowMajorStoreNonSquare)
{
  constexpr size_t NX = 3;// width
  constexpr size_t NY = 5;// height (non-square: NX != NY)
  SipiImage img1(NX, NY, 1, 8, PhotometricInterpretation::MINISBLACK);
  SipiImage img2(NX, NY, 1, 8, PhotometricInterpretation::MINISBLACK);

  // Constructor does not zero-initialize the buffer — fill every cell.
  for (size_t y = 0; y < NY; ++y) {
    for (size_t x = 0; x < NX; ++x) {
      img1.setPixel(x, y, 0, 40);
      img2.setPixel(x, y, 0, 40);
    }
  }
  img1.setPixel(2, 1, 0, 250);// one distinctive sample, off-diagonal (x != y)

  const std::optional<PixelDelta> delta = img2.maxPixelDelta(img1);
  ASSERT_TRUE(delta.has_value());
  EXPECT_EQ(delta->max_abs, 250 - 40);
  EXPECT_EQ(delta->max_x, 2u);
  EXPECT_EQ(delta->max_y, 1u);
}

// getPixel and setPixel must be exact inverses across a non-square,
// multi-channel image — pinning both the spatial (`y * nx + x`) and the
// channel-interleaving (`nc * … + c`) terms, and guarding against a future
// change that corrects only one accessor.
TEST(PixelAccessor, GetSetRoundTripNonSquareMultiChannel)
{
  constexpr size_t NX = 3;
  constexpr size_t NY = 2;
  constexpr size_t NC = 3;
  SipiImage img(NX, NY, NC, 8, PhotometricInterpretation::RGB);

  // Encode each (cell, channel) into a distinct value (max 5*10+2+1 = 53).
  const auto value = [](size_t x, size_t y, size_t c) { return static_cast<int>((y * NX + x) * 10 + c) + 1; };

  for (size_t y = 0; y < NY; ++y) {
    for (size_t x = 0; x < NX; ++x) {
      for (size_t c = 0; c < NC; ++c) { img.setPixel(x, y, c, value(x, y, c)); }
    }
  }
  for (size_t y = 0; y < NY; ++y) {
    for (size_t x = 0; x < NX; ++x) {
      for (size_t c = 0; c < NC; ++c) { EXPECT_EQ(img.getPixel(x, y, c), value(x, y, c)); }
    }
  }
}

// Same round-trip on the 16-bit branch of the accessors (same shape; only
// `bps` and the value scale differ).
TEST(PixelAccessor, GetSetRoundTripNonSquareMultiChannelSixteenBit)
{
  constexpr size_t NX = 3;
  constexpr size_t NY = 2;
  constexpr size_t NC = 3;
  SipiImage img(NX, NY, NC, 16, PhotometricInterpretation::RGB);

  // Encode each (cell, channel) into a distinct value (max 5*1000+2+1 = 5003).
  const auto value = [](size_t x, size_t y, size_t c) { return static_cast<int>((y * NX + x) * 1000 + c) + 1; };

  for (size_t y = 0; y < NY; ++y) {
    for (size_t x = 0; x < NX; ++x) {
      for (size_t c = 0; c < NC; ++c) { img.setPixel(x, y, c, value(x, y, c)); }
    }
  }
  for (size_t y = 0; y < NY; ++y) {
    for (size_t x = 0; x < NX; ++x) {
      for (size_t c = 0; c < NC; ++c) { EXPECT_EQ(img.getPixel(x, y, c), value(x, y, c)); }
    }
  }
}

}// namespace
