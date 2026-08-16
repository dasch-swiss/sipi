/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include "SipiPeakMemory.h"

using namespace Sipi;

// --- Full resolution, no transforms ---

TEST(PeakMemory, FullMaxNoTransformPeakEqualsDecodeBuffer)
{
  // /full/max/0/default.jpg — decode 512x512, 3ch 8bit, no scale/rotate/icc
  size_t peak = estimate_peak_memory(512, 512, 0, 0, 3, 8, 0.0, false);
  size_t decode_buf = 512 * 512 * 3;// 786432
  // Peak should be the decode buffer (first alloc, nothing else)
  EXPECT_EQ(peak, decode_buf);
}

// --- Rotation ---

TEST(PeakMemory, FullMaxWithRotation90PeakIsDoubleDecodeBuffer)
{
  // /full/max/90/default.jpg — 512x512, 3ch 8bit, rotation=90
  size_t peak = estimate_peak_memory(512, 512, 0, 0, 3, 8, 90.0, false);
  size_t decode_buf = 512 * 512 * 3;
  // Peak should be decode + rotated (same size, swapped dims)
  // scale_peak = decode + 0 + 0 (no scaling)
  // rotate_peak = decode + rotated = 2 * decode
  EXPECT_EQ(peak, 2 * decode_buf);
}

TEST(PeakMemory, ArbitraryRotationExpandsDimensions)
{
  // 45° rotation: bounding box = diagonal
  size_t peak = estimate_peak_memory(100, 100, 0, 0, 3, 8, 45.0, false);
  size_t decode_buf = 100 * 100 * 3;

  // Diagonal of 100x100 = ~141.4, ceil = 142
  size_t diag = 142;
  size_t rotated_buf = diag * diag * 3;
  // rotate peak = decode + rotated
  EXPECT_GE(peak, decode_buf + rotated_buf - 100);// allow small rounding
  EXPECT_GT(peak, 2 * decode_buf);// definitely larger than 2x
}

// --- Tile request (tiny budget) ---

TEST(PeakMemory, TileRequestPeakIsTiny)
{
  // /0,0,1024,1024/1024,/0/default.jpg on 20000x30000 JP2
  // After reduce, decode dims are ~1024x1024
  size_t peak = estimate_peak_memory(1024, 1024, 1024, 1024, 3, 8, 0.0, false);
  // Should be a few MB, not GB
  EXPECT_LT(peak, 10 * 1024 * 1024);// < 10 MB
}

// --- Thumbnail (tiny budget) ---

TEST(PeakMemory, ThumbnailPeakIsTiny)
{
  // /full/,128/0/default.jpg — decode at reduce=5 gives ~625x937
  // But actual decode with region might be even smaller
  size_t peak = estimate_peak_memory(128, 128, 128, 128, 3, 8, 0.0, false);
  EXPECT_LT(peak, 1 * 1024 * 1024);// < 1 MB
}

// --- High quality downscale ---

TEST(PeakMemory, HighQualityDownscalePeakIs1_5x)
{
  // Decode 2000x2000, scale to 500x500 — 2-stage downscale
  size_t peak = estimate_peak_memory(2000, 2000, 500, 500, 3, 8, 0.0, false);
  size_t decode_buf = 2000 * 2000 * 3;
  size_t scaled_buf = 500 * 500 * 3;
  // scale_peak = decode + scaled + scaled/2
  size_t expected_scale_peak = decode_buf + scaled_buf + scaled_buf / 2;
  EXPECT_EQ(peak, expected_scale_peak);
}

// --- ICC conversion adds final buffer ---

TEST(PeakMemory, ICCConversionAddsFinalBuffer)
{
  // Small image with ICC conversion
  size_t peak_with_icc = estimate_peak_memory(256, 256, 0, 0, 3, 8, 0.0, true);
  size_t peak_without_icc = estimate_peak_memory(256, 256, 0, 0, 3, 8, 0.0, false);
  // With ICC, peak should be larger (rotate + final buffers)
  EXPECT_GE(peak_with_icc, peak_without_icc);
}

// --- 16-bit images ---

TEST(PeakMemory, SixteenBitImageDoublesMemory)
{
  size_t peak_8bit = estimate_peak_memory(512, 512, 0, 0, 3, 8, 0.0, false);
  size_t peak_16bit = estimate_peak_memory(512, 512, 0, 0, 3, 16, 0.0, false);
  EXPECT_EQ(peak_16bit, peak_8bit * 2);
}

// --- Zero/default nc and bps ---

TEST(PeakMemory, ZeroNcDefaultsToFourChannels)
{
  size_t peak = estimate_peak_memory(100, 100, 0, 0, 0, 8, 0.0, false);
  // nc=0 defaults to 4 channels
  size_t expected = 100 * 100 * 4;
  EXPECT_EQ(peak, expected);
}
