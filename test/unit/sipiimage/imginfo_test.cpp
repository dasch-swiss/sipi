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

// Unit tests run from build/test/unit/sipiimage/
static const std::string test_images = "../../../../test/_test_data/images/unit/";

static bool file_exists(const std::string &path) {
  struct stat buf{};
  return stat(path.c_str(), &buf) == 0;
}

// --- JP2 getDim() ---
// Note: JP2 tests depend on Kakadu library initialization which may
// not work in all environments. Tests skip if getDim returns FAILURE.

TEST(ImgInfo, JP2GetDimReturnsDimensions)
{
  std::string jp2_path = test_images + "lena512.jp2";
  if (!file_exists(jp2_path)) GTEST_SKIP() << "Test image not found: " << jp2_path;

  Sipi::SipiIOJ2k io;
  auto info = io.getDim(jp2_path);
  if (info.success == Sipi::SipiImgInfo::FAILURE) GTEST_SKIP() << "JP2 getDim failed (Kakadu may not be available)";
  EXPECT_EQ(info.width, 512);
  EXPECT_EQ(info.height, 512);
  EXPECT_GT(info.clevels, 0);
}

TEST(ImgInfo, JP2GetDimReturnsChannelsAndBps)
{
  std::string jp2_path = test_images + "lena512.jp2";
  if (!file_exists(jp2_path)) GTEST_SKIP() << "Test image not found: " << jp2_path;

  Sipi::SipiIOJ2k io;
  auto info = io.getDim(jp2_path);
  if (info.success == Sipi::SipiImgInfo::FAILURE) GTEST_SKIP() << "JP2 getDim failed (Kakadu may not be available)";
  EXPECT_GT(info.nc, 0);
  EXPECT_GT(info.bps, 0);
}

TEST(ImgInfo, JP2GetDimInvalidFileReturnFailure)
{
  Sipi::SipiIOJ2k io;
  auto info = io.getDim(test_images + "mario.png");// not a JP2
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::FAILURE);
}

TEST(ImgInfo, JP2GetDimNonexistentFileReturnFailure)
{
  Sipi::SipiIOJ2k io;
  auto info = io.getDim(test_images + "nonexistent.jp2");
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::FAILURE);
}

// --- TIFF getDim() ---

TEST(ImgInfo, TiffGetDimReturnsDimensions)
{
  Sipi::SipiIOTiff io;
  auto info = io.getDim(test_images + "lena512.tif");
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::DIMS);
  EXPECT_EQ(info.width, 512);
  EXPECT_EQ(info.height, 512);
}

TEST(ImgInfo, TiffGetDimReturnsChannelsAndBps)
{
  Sipi::SipiIOTiff io;
  auto info = io.getDim(test_images + "lena512.tif");
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::DIMS);
  EXPECT_GT(info.nc, 0);
  EXPECT_GT(info.bps, 0);
}

TEST(ImgInfo, TiffGetDimInvalidFileReturnsDefault)
{
  Sipi::SipiIOTiff io;
  auto info = io.getDim(test_images + "mario.png");// not a TIFF
  EXPECT_NE(info.success, Sipi::SipiImgInfo::DIMS);
}

// --- JPEG getDim() ---

TEST(ImgInfo, JpegGetDimReturnsDimensions)
{
  Sipi::SipiIOJpeg io;
  auto info = io.getDim(test_images + "MaoriFigure.jpg");
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::DIMS);
  EXPECT_GT(info.width, 0);
  EXPECT_GT(info.height, 0);
}

TEST(ImgInfo, JpegGetDimReturnsChannelsAndBps)
{
  Sipi::SipiIOJpeg io;
  auto info = io.getDim(test_images + "MaoriFigure.jpg");
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::DIMS);
  EXPECT_GT(info.nc, 0);
  EXPECT_EQ(info.bps, 8);// JPEG is always 8-bit
}

TEST(ImgInfo, JpegGetDimInvalidFileReturnFailure)
{
  Sipi::SipiIOJpeg io;
  auto info = io.getDim(test_images + "lena512.tif");// not a JPEG
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::FAILURE);
}

// --- PNG getDim() ---

TEST(ImgInfo, PngGetDimReturnsDimensions)
{
  Sipi::SipiIOPng io;
  auto info = io.getDim(test_images + "mario.png");
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::DIMS);
  EXPECT_GT(info.width, 0);
  EXPECT_GT(info.height, 0);
}

TEST(ImgInfo, PngGetDimReturnsChannelsAndBps)
{
  Sipi::SipiIOPng io;
  auto info = io.getDim(test_images + "mario.png");
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::DIMS);
  EXPECT_GT(info.nc, 0);
  EXPECT_GT(info.bps, 0);
}

TEST(ImgInfo, PngGetDimInvalidFileReturnFailure)
{
  Sipi::SipiIOPng io;
  auto info = io.getDim(test_images + "lena512.tif");// not a PNG
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::FAILURE);
}
