/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Regression test — `SipiImage::operator==` compares the pixel buffers of
 * both operands in every bits-per-sample arm.
 *
 * Before the fix the 16 bps arm aliased both comparison pointers to the
 * left-hand buffer, so two 16 bps images with equal dimensions always
 * compared equal regardless of pixel content. The 8 bps arm was correct,
 * which is why the fixture-based equality tests (all 8 bps) never caught it.
 */

#include <gtest/gtest.h>

#include "../../../src/SipiImage.h"

namespace {

using Sipi::PhotometricInterpretation;
using Sipi::SipiImage;

TEST(Equality, SixteenBpsDetectsDifferingPixels)
{
  SipiImage img1(3, 2, 3, 16, PhotometricInterpretation::RGB);
  SipiImage img2(3, 2, 3, 16, PhotometricInterpretation::RGB);
  EXPECT_TRUE(img1 == img2);

  img2.setPixel(2, 1, 1, 0x1234);
  EXPECT_FALSE(img1 == img2);
}

TEST(Equality, EightBpsDetectsDifferingPixels)
{
  SipiImage img1(3, 2, 3, 8, PhotometricInterpretation::RGB);
  SipiImage img2(3, 2, 3, 8, PhotometricInterpretation::RGB);
  EXPECT_TRUE(img1 == img2);

  img2.setPixel(2, 1, 1, 0x42);
  EXPECT_FALSE(img1 == img2);
}

}// namespace
