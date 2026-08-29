/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Regression test — pixel-buffer allocations guarded against integer
 * overflow (DEV-6063). Every `nx * ny * nc * elem`-shaped allocation in
 * SipiImage.cpp now funnels through `Sipi::checked_buf_size`, which rejects
 * (rather than silently wrapping) the moment any factor overflows `size_t`.
 * These tests assert a thrown `SipiImageError`, never a crash or a
 * clamped/undersized allocation.
 */

#include <cstddef>
#include <limits>

#include <gtest/gtest.h>

#include "error/SipiValueError.h"
#include "image/SipiImage.h"
#include "image/SipiImageError.h"
#include "image_processing/processing.h"

namespace {

using Sipi::ErrorCode;
using Sipi::PhotometricInterpretation;
using Sipi::SipiImage;
using Sipi::SipiImageError;

TEST(OverflowRegression, ConstructorRejectsOverflowingDimensions)
{
  // nx * ny alone overflows size_t; a wrapped product would otherwise
  // allocate a tiny buffer that later pixel writes would overrun.
  EXPECT_THROW(SipiImage(std::numeric_limits<size_t>::max(), 2, 1, 8, PhotometricInterpretation::MINISBLACK),
    SipiImageError);
}

TEST(OverflowRegression, ConstructorRejectsOverflowingChannelCount)
{
  // nx * ny fits, but multiplying in nc pushes the product past SIZE_MAX.
  // SEPARATED carries no nc-count precondition in the constructor, so this
  // exercises the overflow guard rather than an unrelated validation branch.
  constexpr size_t huge_nc = std::numeric_limits<size_t>::max() / 2;
  EXPECT_THROW(SipiImage(2, 2, huge_nc, 8, PhotometricInterpretation::SEPARATED), SipiImageError);
}

// crop(int x, int y, size_t width, size_t height): a negative x/y must be
// validated BEFORE combining with the unsigned width/height. Before the
// fix, `width += x` (x negative, converted to a huge size_t) could wrap the
// requested crop region back out to the full image instead of being
// rejected, silently returning image content outside the caller's intent.
TEST(CropRegression, NegativeXBeyondRequestedWidthIsRejected)
{
  SipiImage img(4, 4, 1, 8, PhotometricInterpretation::MINISBLACK);
  const auto result = Sipi::processing::crop(img, -100, 0, 5, 4);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::kMalformedInput);
}

TEST(CropRegression, NegativeYBeyondRequestedHeightIsRejected)
{
  SipiImage img(4, 4, 1, 8, PhotometricInterpretation::MINISBLACK);
  const auto result = Sipi::processing::crop(img, 0, -100, 4, 5);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::kMalformedInput);
}

// crop(): every currently-registered read path normalizes bits/sample to 8
// or 16 before pixel content ever reaches this function, so an unsupported
// bps is a defensive case, not a reachable one. It must still be rejected
// rather than silently skipping the crop: SipiImage's constructor only
// accepts 8 or 16, so an image with an unsupported bps is built via
// set_pixels() (which does not itself constrain bps), matching a
// hypothetical decoder that has not normalized its output.
TEST(CropRegression, UnsupportedBitsPerSampleIsRejected)
{
  SipiImage img(4, 4, 1, 8, PhotometricInterpretation::MINISBLACK);
  std::vector<Sipi::byte> buf(4 * 4 * 1 * 4, 0);
  img.set_pixels(std::move(buf), 4, 4, 1, 32);
  const auto result = Sipi::processing::crop(img, 0, 0, 2, 2);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::kMalformedInput);
}

}// namespace
