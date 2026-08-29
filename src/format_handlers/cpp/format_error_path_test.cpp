/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <png.h>

#include "image/SipiImage.h"
#include "image/SipiImageError.h"
#include "SipiIOJpeg.h"
#include "SipiIOPng.h"
#include "SipiIOTiff.h"
#include "test_paths.h"

// `sipi::test::{data_dir,tmp_dir}` honour Bazel's SIPI_TEST_DATA_DIR /
// TEST_TMPDIR envs and fall back to the historical CMake build-tree
// relative paths when unset. See test/test_paths.h.
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
  // The PNG setjmp landing site funnels every libpng decode error into a
  // single error Result (kDecodeFailed) — nothing throws for this fixture.
  const auto r = img.read(truncated);
  EXPECT_FALSE(r.has_value());

  std::remove(truncated.c_str());
}

TEST(PngErrorPath, TruncatedPngReadShapeReturnsFailure)
{
  const std::string src = test_images + "unit/mario.png";
  const std::string truncated = tmp_dir + "_truncated_read_shape.png";
  ASSERT_TRUE(file_exists(src));

  // Truncate after the PNG signature (8 bytes) but before IHDR is complete
  ASSERT_FALSE(create_truncated(src, truncated, 20).empty());

  Sipi::SipiIOPng io;
  auto result = io.read_shape(truncated);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->success, Sipi::SipiImgInfo::FAILURE);

  std::remove(truncated.c_str());
}

TEST(PngErrorPath, NonPngFileReadShapeReturnsFailure)
{
  Sipi::SipiIOPng io;
  auto result = io.read_shape(test_images + "unit/lena512.tif");  // not a PNG
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->success, Sipi::SipiImgInfo::FAILURE);
}

/// Writes a 1x1 RGB PNG carrying a "Raw profile type iptc" tEXt chunk with
/// the given (attacker-controlled) raw-profile payload.
static bool write_png_with_raw_profile_text(const std::string &path, const std::string &raw_profile_text)
{
  FILE *fp = fopen(path.c_str(), "wb");
  if (fp == nullptr) return false;

  png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (png_ptr == nullptr) {
    fclose(fp);
    return false;
  }
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == nullptr) {
    png_destroy_write_struct(&png_ptr, nullptr);
    fclose(fp);
    return false;
  }
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return false;
  }

  png_init_io(png_ptr, fp);
  png_set_IHDR(png_ptr,
    info_ptr,
    1,
    1,
    8,
    PNG_COLOR_TYPE_RGB,
    PNG_INTERLACE_NONE,
    PNG_COMPRESSION_TYPE_BASE,
    PNG_FILTER_TYPE_BASE);

  png_text text;
  std::memset(&text, 0, sizeof(text));
  text.compression = PNG_TEXT_COMPRESSION_NONE;
  text.key = const_cast<char *>("Raw profile type iptc");
  text.text = const_cast<char *>(raw_profile_text.c_str());
  text.text_length = raw_profile_text.size();
  png_set_text(png_ptr, info_ptr, &text, 1);

  png_write_info(png_ptr, info_ptr);
  std::vector<unsigned char> row(3, 0);
  png_write_row(png_ptr, row.data());
  png_write_end(png_ptr, nullptr);
  png_destroy_write_struct(&png_ptr, &info_ptr);
  fclose(fp);
  return true;
}

// Regression test for an unbounded `reserve()` in decode_raw_profile(): a
// "Raw profile type iptc" chunk can declare a huge decimal length while the
// chunk itself carries only a handful of hex bytes.
TEST(PngErrorPath, RawProfileHugeDeclaredLengthDoesNotOverAllocate)
{
  const std::string path = tmp_dir + "_raw_profile_huge_length.png";
  const std::string raw_profile = std::string("\n") + "iptc" + "\n" + "999999999999" + "\n" + "ab" + "\n";
  ASSERT_TRUE(write_png_with_raw_profile_text(path, raw_profile));

  Sipi::SipiIOPng io;
  Sipi::SipiImage img;
  // A bounded error Result or a small/empty success are both acceptable —
  // the point is that decoding returns promptly without an unbounded
  // reservation attempt (std::bad_alloc / std::length_error / crash).
  EXPECT_NO_THROW({
    [[maybe_unused]] auto r = io.Sipi::SipiIO::read(&img, path);
  });

  std::remove(path.c_str());
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
  // Truncated TIFFs should fail cleanly (not crash with SIGABRT/SIGSEGV): most
  // decode failures are an error Result, but a handful of scanline/tile
  // helpers stay exception-based, so either outcome is acceptable here.
  try {
    [[maybe_unused]] auto r = img.read(truncated);
  } catch (const std::exception &) {
    // Throwing is expected and acceptable
  }
  // If we got here without SIGABRT or SIGSEGV, the test passes.

  std::remove(truncated.c_str());
}

TEST(TiffErrorPath, TruncatedTiffReadShapeHandledCleanly)
{
  const std::string src = test_images + "unit/lena512.tif";
  const std::string truncated = tmp_dir + "_truncated_read_shape.tif";
  ASSERT_TRUE(file_exists(src));

  ASSERT_FALSE(create_truncated(src, truncated, 50).empty());

  Sipi::SipiIOTiff io;
  auto result = io.read_shape(truncated);
  // A successful probe reporting FAILURE (TIFFOpen cannot read the truncated
  // directory), not a crash and not DIMS.
  ASSERT_TRUE(result.has_value());
  EXPECT_NE(result->success, Sipi::SipiImgInfo::DIMS);

  std::remove(truncated.c_str());
}

// ============================================================
// JPEG read_shape error-path test — exercises setjmp in read_shape
// ============================================================

TEST(JpegErrorPath, TruncatedJpegReadShapeReturnsFailure)
{
  const std::string src = test_images + "unit/MaoriFigure.jpg";
  const std::string truncated = tmp_dir + "_truncated_read_shape.jpg";
  ASSERT_TRUE(file_exists(src));

  ASSERT_FALSE(create_truncated(src, truncated, 100).empty());

  Sipi::SipiIOJpeg io;
  auto result = io.read_shape(truncated);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->success, Sipi::SipiImgInfo::FAILURE);

  std::remove(truncated.c_str());
}

// ============================================================
// message() path redaction — client-facing accessors strip
// filesystem directory prefixes; to_string()/what() keep them.
// ============================================================

TEST(SipiImageErrorPathRedaction, MessageStripsDirectoryPrefixKeepsFilename)
{
  const Sipi::SipiImageError err{ "Cannot read file \"/srv/images/sub/foo.jp2\": broken" };

  const std::string msg = err.message();
  EXPECT_EQ(msg.find("/srv/images/"), std::string::npos);
  EXPECT_NE(msg.find("foo.jp2"), std::string::npos);

  const std::string full = err.to_string();
  EXPECT_NE(full.find("/srv/images/sub/foo.jp2"), std::string::npos);
}
