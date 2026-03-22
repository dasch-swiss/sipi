/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include "SipiDecodeDims.h"
#include "SipiRegion.h"
#include "SipiSize.h"

using namespace Sipi;

// --- Full region, full size (no transforms) ---

TEST(DecodeDims, FullRegionFullSizeReturnsSourceDims)
{
  auto region = std::make_shared<SipiRegion>();// default = FULL
  auto size = std::make_shared<SipiSize>();// default = FULL

  auto dims = compute_decode_dims(20000, 30000, 5, region, size);

  EXPECT_EQ(dims.width, 20000);
  EXPECT_EQ(dims.height, 30000);
  EXPECT_EQ(dims.reduce, 0);
  EXPECT_EQ(dims.out_w, 20000);
  EXPECT_EQ(dims.out_h, 30000);
}

// --- Full region with size → reduced dims ---

TEST(DecodeDims, FullRegionWithSizeReturnsReducedDims)
{
  auto region = std::make_shared<SipiRegion>();// FULL
  // Request width 625 from a 20000-wide image with 5 DWT levels
  // 20000/32 = 625, reduce=5, but this depends on size->get_size internals
  auto size = std::make_shared<SipiSize>(",625");// height=625

  auto dims = compute_decode_dims(20000, 30000, 5, region, size);

  // With reduce levels, decode dims should be much smaller than source
  EXPECT_GT(dims.reduce, 0);
  EXPECT_LT(dims.width, 20000);
  EXPECT_LT(dims.height, 30000);
  EXPECT_EQ(dims.out_h, 625);
}

// --- Region crops before reduce ---

TEST(DecodeDims, RegionCropsBeforeReduce)
{
  auto region = std::make_shared<SipiRegion>(0, 0, 1024, 1024);// 1024x1024 from source
  auto size = std::make_shared<SipiSize>("1024,");// width=1024

  auto dims = compute_decode_dims(20000, 30000, 5, region, size);

  // Region is 1024x1024, requesting 1024 wide: reduce should be 0
  EXPECT_EQ(dims.region_w, 1024);
  EXPECT_EQ(dims.region_h, 1024);
  // Decode dims should be close to 1024 (reduce=0 means no reduction)
  EXPECT_LE(dims.width, 1024);
  EXPECT_LE(dims.height, 1024);
}

// --- Reduce level never exceeds clevels ---

TEST(DecodeDims, ReduceLevelNeverExceedsClevels)
{
  auto region = std::make_shared<SipiRegion>();
  // Request very small output from large image
  auto size = std::make_shared<SipiSize>(",10");

  int clevels = 3;// only 3 DWT levels available
  auto dims = compute_decode_dims(20000, 30000, clevels, region, size);

  EXPECT_LE(dims.reduce, clevels);
}

// --- Reduce level zero for full/max ---

TEST(DecodeDims, ReduceLevelZeroForFullMax)
{
  auto region = std::make_shared<SipiRegion>();
  auto size = std::make_shared<SipiSize>();// FULL = max

  auto dims = compute_decode_dims(20000, 30000, 5, region, size);

  EXPECT_EQ(dims.reduce, 0);
  EXPECT_EQ(dims.width, 20000);
  EXPECT_EQ(dims.height, 30000);
}

// --- Nullptr region/size treated as FULL ---

TEST(DecodeDims, NullptrRegionSizeTreatedAsFull)
{
  auto dims = compute_decode_dims(512, 512, 3, nullptr, nullptr);

  EXPECT_EQ(dims.width, 512);
  EXPECT_EQ(dims.height, 512);
  EXPECT_EQ(dims.reduce, 0);
}

// --- Zero clevels means no reduce ---

TEST(DecodeDims, ZeroClevelsDisablesReduce)
{
  auto region = std::make_shared<SipiRegion>();
  auto size = std::make_shared<SipiSize>(",128");

  auto dims = compute_decode_dims(20000, 30000, 0, region, size);

  // Without clevels, no reduce is possible — decode at full resolution
  EXPECT_EQ(dims.reduce, 0);
  EXPECT_EQ(dims.width, 20000);
  EXPECT_EQ(dims.height, 30000);
}

// --- Small image, no reduce needed ---

TEST(DecodeDims, SmallImageNoReduceNeeded)
{
  auto region = std::make_shared<SipiRegion>();
  auto size = std::make_shared<SipiSize>("256,");

  auto dims = compute_decode_dims(256, 256, 5, region, size);

  // 256 requested from 256 source: no reduce
  EXPECT_EQ(dims.reduce, 0);
  EXPECT_EQ(dims.width, 256);
  EXPECT_EQ(dims.height, 256);
}
