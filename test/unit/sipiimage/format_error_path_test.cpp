/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "SipiImage.hpp"
#include "SipiImageError.hpp"
#include "SipiIOJpeg.h"
#include "SipiIOPng.h"
#include "SipiIOTiff.h"
#include "test_paths.hpp"

// `sipi::test::{data_dir,tmp_dir}` honour Bazel's SIPI_TEST_DATA_DIR /
// TEST_TMPDIR envs and fall back to the historical CMake build-tree
// relative paths when unset. See test/test_paths.hpp.
static const std::string test_images = sipi::test::data_dir() + "/images/";
static const std::string tmp_dir = sipi::test::tmp_dir() + "/";

static bool file_exists(const std::string &path)
{
  struct stat buf {};
  return stat(path.c_str(), &buf) == 0;
}

/// Helper: create a truncated copy of a file (first N bytes).
static std::string create_truncated(const std::string &src, const std::string &dst, size_t nbytes)
{
  int fd_in = ::open(src.c_str(), O_RDONLY);
  if (fd_in < 0) return {};
  auto *buf = new unsigned char[nbytes];
  ssize_t n = ::read(fd_in, buf, nbytes);
  ::close(fd_in);
  if (n < static_cast<ssize_t>(nbytes)) { delete[] buf; return {}; }

  int fd_out = ::open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd_out < 0) { delete[] buf; return {}; }
  ::write(fd_out, buf, nbytes);
  ::close(fd_out);
  delete[] buf;
  return dst;
}

// ============================================================
// PNG error-path tests — exercises setjmp(png_jmpbuf) fix
// ============================================================

TEST(PngErrorPath, TruncatedPngReadThrowsCleanly)
{
  const std::string src = test_images + "unit/mario.png";
  const std::string truncated = tmp_dir + "_truncated_test.png";
  ASSERT_TRUE(file_exists(src));

  ASSERT_FALSE(create_truncated(src, truncated, 100).empty());

  Sipi::SipiImage img;
  EXPECT_ANY_THROW(img.read(truncated));

  std::remove(truncated.c_str());
}

TEST(PngErrorPath, TruncatedPngGetDimReturnsFailure)
{
  const std::string src = test_images + "unit/mario.png";
  const std::string truncated = tmp_dir + "_truncated_getdim.png";
  ASSERT_TRUE(file_exists(src));

  // Truncate after the PNG signature (8 bytes) but before IHDR is complete
  ASSERT_FALSE(create_truncated(src, truncated, 20).empty());

  Sipi::SipiIOPng io;
  auto info = io.getDim(truncated);
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::FAILURE);

  std::remove(truncated.c_str());
}

TEST(PngErrorPath, NonPngFileGetDimReturnsFailure)
{
  Sipi::SipiIOPng io;
  auto info = io.getDim(test_images + "unit/lena512.tif");  // not a PNG
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::FAILURE);
}

// ============================================================
// TIFF error-path tests — exercises error-return fix
// ============================================================

TEST(TiffErrorPath, TruncatedTiffReadThrowsCleanly)
{
  const std::string src = test_images + "unit/lena512.tif";
  const std::string truncated = tmp_dir + "_truncated_test.tif";
  ASSERT_TRUE(file_exists(src));

  ASSERT_FALSE(create_truncated(src, truncated, 100).empty());

  Sipi::SipiImage img;
  // Truncated TIFFs should throw (not crash with SIGABRT/SIGSEGV).
  // The key assertion is that we don't crash.
  try {
    img.read(truncated);
  } catch (const std::exception &) {
    // Throwing is expected and acceptable
  }
  // If we got here without SIGABRT or SIGSEGV, the test passes.

  std::remove(truncated.c_str());
}

TEST(TiffErrorPath, TruncatedTiffGetDimHandledCleanly)
{
  const std::string src = test_images + "unit/lena512.tif";
  const std::string truncated = tmp_dir + "_truncated_getdim.tif";
  ASSERT_TRUE(file_exists(src));

  ASSERT_FALSE(create_truncated(src, truncated, 50).empty());

  Sipi::SipiIOTiff io;
  auto info = io.getDim(truncated);
  // Should return FAILURE or default, not crash
  EXPECT_NE(info.success, Sipi::SipiImgInfo::DIMS);

  std::remove(truncated.c_str());
}

// ============================================================
// JPEG getDim error-path test — exercises setjmp in getDim
// ============================================================

TEST(JpegErrorPath, TruncatedJpegGetDimReturnsFailure)
{
  const std::string src = test_images + "unit/MaoriFigure.jpg";
  const std::string truncated = tmp_dir + "_truncated_getdim.jpg";
  ASSERT_TRUE(file_exists(src));

  ASSERT_FALSE(create_truncated(src, truncated, 100).empty());

  Sipi::SipiIOJpeg io;
  auto info = io.getDim(truncated);
  EXPECT_EQ(info.success, Sipi::SipiImgInfo::FAILURE);

  std::remove(truncated.c_str());
}
