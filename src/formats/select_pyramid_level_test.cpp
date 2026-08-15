/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gtest/gtest.h"

#include "SipiIO.h"
#include "formats/SipiIOTiff.h"

using Sipi::select_pyramid_level;
using Sipi::SubImageInfo;

namespace {

// A 4-level dyadic pyramid: full, /2, /4, /8. Only `reduce` (the ratio) matters
// to the selector; the other fields are illustrative.
std::vector<SubImageInfo> dyadic_pyramid()
{
  return {
    { 1, 512, 512, 256, 256 },
    { 2, 256, 256, 256, 256 },
    { 4, 128, 128, 256, 256 },
    { 8, 64, 64, 256, 256 },
  };
}

}// namespace

// Full-size (exp 0) and negative exponents always resolve to the full level.
TEST(SelectPyramidLevel, FullSizeIsLevelZero)
{
  const auto p = dyadic_pyramid();
  EXPECT_EQ(select_pyramid_level(p, 0), 0u);
  EXPECT_EQ(select_pyramid_level(p, -1), 0u);
}

// Each exact power-of-two exponent picks its matching IFD.
TEST(SelectPyramidLevel, ExactMatches)
{
  const auto p = dyadic_pyramid();
  EXPECT_EQ(select_pyramid_level(p, 1), 1u);// /2
  EXPECT_EQ(select_pyramid_level(p, 2), 2u);// /4
  EXPECT_EQ(select_pyramid_level(p, 3), 3u);// /8
}

// A request deeper than the pyramid clamps to the smallest available level; the
// caller then applies a residual scale.
TEST(SelectPyramidLevel, DeeperThanPyramidClampsToSmallest)
{
  const auto p = dyadic_pyramid();
  EXPECT_EQ(select_pyramid_level(p, 4), 3u);// /16 requested, /8 is the deepest
  EXPECT_EQ(select_pyramid_level(p, 30), 3u);
}

// A non-dyadic pyramid (missing /4) falls back to the largest ratio ≤ divisor.
TEST(SelectPyramidLevel, NonDyadicFallsBackToLargestNotExceeding)
{
  const std::vector<SubImageInfo> p{
    { 1, 800, 600, 256, 256 },
    { 2, 400, 300, 256, 256 },
    { 8, 100, 75, 256, 256 },// jumps straight to /8, no /4 level
  };
  // exp 2 => divisor 4; no ratio-4 level, so pick the /2 level (ratio 2 ≤ 4).
  EXPECT_EQ(select_pyramid_level(p, 2), 1u);
  // exp 3 => divisor 8; exact /8 level.
  EXPECT_EQ(select_pyramid_level(p, 3), 2u);
}

// A single-level (non-pyramidal) TIFF always decodes from level 0.
TEST(SelectPyramidLevel, SingleLevelAlwaysZero)
{
  const std::vector<SubImageInfo> p{ { 1, 512, 512, 0, 0 } };
  EXPECT_EQ(select_pyramid_level(p, 0), 0u);
  EXPECT_EQ(select_pyramid_level(p, 3), 0u);
}

// Empty resolutions never indexes out of range.
TEST(SelectPyramidLevel, EmptyIsZero)
{
  EXPECT_EQ(select_pyramid_level({}, 2), 0u);
}
