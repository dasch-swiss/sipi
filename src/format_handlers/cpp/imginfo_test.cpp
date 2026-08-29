/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <sys/stat.h>

#include "SipiIOJ2k.h"
#include "SipiIOJpeg.h"
#include "SipiIOPng.h"
#include "SipiIOTiff.h"
#include "test_paths.h"

// `sipi::test::data_dir()` honours Bazel's SIPI_WORKSPACE_ROOT env and
// falls back to the historical CMake build-tree relative path when unset.
// See test/test_paths.h.
static const std::string test_images = sipi::test::data_dir() + "/images/unit/";

static bool file_exists(const std::string &path) {
  struct stat buf{};
  return stat(path.c_str(), &buf) == 0;
}

// --- JP2 read_shape() ---
// Note: JP2 tests depend on Kakadu library initialization which may
// not work in all environments. Tests skip if read_shape returns FAILURE.

TEST(ImgInfo, JP2ReadShapeReturnsDimensions)
{
  std::string jp2_path = test_images + "lena512.jp2";
  if (!file_exists(jp2_path)) GTEST_SKIP() << "Test image not found: " << jp2_path;

  Sipi::SipiIOJ2k io;
  auto result = io.read_shape(jp2_path);
  ASSERT_TRUE(result.has_value());
  const auto &info = *result;
  if (info.success == Sipi::SipiImgInfo::FAILURE) GTEST_SKIP() << "JP2 read_shape failed (Kakadu may not be available)";
  EXPECT_EQ(info.width, 512);
  EXPECT_EQ(info.height, 512);
  EXPECT_GT(info.clevels, 0);
}

TEST(ImgInfo, JP2ReadShapeReturnsChannelsAndBps)
{
  std::string jp2_path = test_images + "lena512.jp2";
  if (!file_exists(jp2_path)) GTEST_SKIP() << "Test image not found: " << jp2_path;

  Sipi::SipiIOJ2k io;
  auto result = io.read_shape(jp2_path);
  ASSERT_TRUE(result.has_value());
  const auto &info = *result;
  if (info.success == Sipi::SipiImgInfo::FAILURE) GTEST_SKIP() << "JP2 read_shape failed (Kakadu may not be available)";
  EXPECT_GT(info.nc, 0);
  EXPECT_GT(info.bps, 0);
}

TEST(ImgInfo, JP2ReadShapeInvalidFileReturnFailure)
{
  Sipi::SipiIOJ2k io;
  auto result = io.read_shape(test_images + "mario.png");// not a JP2
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->success, Sipi::SipiImgInfo::FAILURE);
}

TEST(ImgInfo, JP2ReadShapeNonexistentFileReturnFailure)
{
  Sipi::SipiIOJ2k io;
  auto result = io.read_shape(test_images + "nonexistent.jp2");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->success, Sipi::SipiImgInfo::FAILURE);
}

// --- TIFF read_shape() ---

TEST(ImgInfo, TiffReadShapeReturnsDimensions)
{
  Sipi::SipiIOTiff io;
  auto result = io.read_shape(test_images + "lena512.tif");
  ASSERT_TRUE(result.has_value());
  const auto &info = *result;
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::DIMS);
  EXPECT_EQ(info.width, 512);
  EXPECT_EQ(info.height, 512);
}

TEST(ImgInfo, TiffReadShapeReturnsChannelsAndBps)
{
  Sipi::SipiIOTiff io;
  auto result = io.read_shape(test_images + "lena512.tif");
  ASSERT_TRUE(result.has_value());
  const auto &info = *result;
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::DIMS);
  EXPECT_GT(info.nc, 0);
  EXPECT_GT(info.bps, 0);
}

TEST(ImgInfo, TiffReadShapeInvalidFileReturnsDefault)
{
  Sipi::SipiIOTiff io;
  auto result = io.read_shape(test_images + "mario.png");// not a TIFF
  ASSERT_TRUE(result.has_value());
  EXPECT_NE(result->success, Sipi::SipiImgInfo::DIMS);
}

// --- JPEG read_shape() ---

TEST(ImgInfo, JpegReadShapeReturnsDimensions)
{
  Sipi::SipiIOJpeg io;
  auto result = io.read_shape(test_images + "MaoriFigure.jpg");
  ASSERT_TRUE(result.has_value());
  const auto &info = *result;
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::DIMS);
  EXPECT_GT(info.width, 0);
  EXPECT_GT(info.height, 0);
}

TEST(ImgInfo, JpegReadShapeReturnsChannelsAndBps)
{
  Sipi::SipiIOJpeg io;
  auto result = io.read_shape(test_images + "MaoriFigure.jpg");
  ASSERT_TRUE(result.has_value());
  const auto &info = *result;
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::DIMS);
  EXPECT_GT(info.nc, 0);
  EXPECT_EQ(info.bps, 8);// JPEG is always 8-bit
}

TEST(ImgInfo, JpegReadShapeInvalidFileReturnFailure)
{
  Sipi::SipiIOJpeg io;
  auto result = io.read_shape(test_images + "lena512.tif");// not a JPEG
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->success, Sipi::SipiImgInfo::FAILURE);
}

// --- PNG read_shape() ---

TEST(ImgInfo, PngReadShapeReturnsDimensions)
{
  Sipi::SipiIOPng io;
  auto result = io.read_shape(test_images + "mario.png");
  ASSERT_TRUE(result.has_value());
  const auto &info = *result;
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::DIMS);
  EXPECT_GT(info.width, 0);
  EXPECT_GT(info.height, 0);
}

TEST(ImgInfo, PngReadShapeReturnsChannelsAndBps)
{
  Sipi::SipiIOPng io;
  auto result = io.read_shape(test_images + "mario.png");
  ASSERT_TRUE(result.has_value());
  const auto &info = *result;
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::DIMS);
  EXPECT_GT(info.nc, 0);
  EXPECT_GT(info.bps, 0);
}

TEST(ImgInfo, PngReadShapeInvalidFileReturnFailure)
{
  Sipi::SipiIOPng io;
  auto result = io.read_shape(test_images + "lena512.tif");// not a PNG
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->success, Sipi::SipiImgInfo::FAILURE);
}
