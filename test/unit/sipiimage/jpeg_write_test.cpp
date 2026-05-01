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
#include "SipiIO.h"
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

static size_t file_size(const std::string &path)
{
  struct stat buf {};
  if (stat(path.c_str(), &buf) != 0) return 0;
  return static_cast<size_t>(buf.st_size);
}

// Check JPEG magic bytes (0xFF 0xD8)
static bool has_jpeg_magic(const std::string &path)
{
  int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) return false;
  unsigned char magic[2] = {0, 0};
  bool ok = (::read(fd, magic, 2) == 2) && magic[0] == 0xFF && magic[1] == 0xD8;
  ::close(fd);
  return ok;
}

// --- A1: JPEG write happy-path tests ---

TEST(JpegWrite, WriteToFileProducesValidJpeg)
{
  const std::string src = test_images + "unit/MaoriFigure.jpg";
  const std::string dst = tmp_dir + "_jpeg_write_test_output.jpg";
  ASSERT_TRUE(file_exists(src)) << "Test image not found: " << src;

  Sipi::SipiImage img;
  ASSERT_NO_THROW(img.read(src));
  ASSERT_NO_THROW(img.write("jpg", dst));

  EXPECT_TRUE(file_exists(dst));
  EXPECT_GT(file_size(dst), 0u);
  EXPECT_TRUE(has_jpeg_magic(dst)) << "Output file does not have JPEG magic bytes";

  std::remove(dst.c_str());
}

TEST(JpegWrite, DifferentQualitySettingsProduceDifferentSizes)
{
  const std::string src = test_images + "unit/MaoriFigure.jpg";
  const std::string dst_low = tmp_dir + "_jpeg_write_q30.jpg";
  const std::string dst_high = tmp_dir + "_jpeg_write_q95.jpg";
  ASSERT_TRUE(file_exists(src));

  Sipi::SipiCompressionParams params_low = {{Sipi::JPEG_QUALITY, "30"}};
  Sipi::SipiCompressionParams params_high = {{Sipi::JPEG_QUALITY, "95"}};

  Sipi::SipiImage img_low;
  img_low.read(src);
  img_low.write("jpg", dst_low, &params_low);

  Sipi::SipiImage img_high;
  img_high.read(src);
  img_high.write("jpg", dst_high, &params_high);

  ASSERT_TRUE(file_exists(dst_low));
  ASSERT_TRUE(file_exists(dst_high));

  size_t size_low = file_size(dst_low);
  size_t size_high = file_size(dst_high);

  EXPECT_GT(size_high, size_low) << "High quality JPEG should be larger than low quality";

  std::remove(dst_low.c_str());
  std::remove(dst_high.c_str());
}

TEST(JpegWrite, ReadWriteRoundtripPreservesDimensions)
{
  const std::string src = test_images + "unit/MaoriFigure.jpg";
  const std::string dst = tmp_dir + "_jpeg_roundtrip.jpg";
  ASSERT_TRUE(file_exists(src));

  Sipi::SipiImage img_orig;
  img_orig.read(src);
  size_t orig_nx = img_orig.getNx();
  size_t orig_ny = img_orig.getNy();

  img_orig.write("jpg", dst);

  Sipi::SipiImage img_rt;
  img_rt.read(dst);

  EXPECT_EQ(img_rt.getNx(), orig_nx);
  EXPECT_EQ(img_rt.getNy(), orig_ny);

  std::remove(dst.c_str());
}

// --- A2: JPEG read error-path tests ---

TEST(JpegRead, TruncatedJpegHandledCleanly)
{
  // Create a truncated JPEG by copying first 100 bytes of a valid JPEG
  const std::string src = test_images + "unit/MaoriFigure.jpg";
  const std::string truncated = tmp_dir + "_truncated_test.jpg";
  ASSERT_TRUE(file_exists(src));

  // Create truncated file
  {
    int fd_in = ::open(src.c_str(), O_RDONLY);
    ASSERT_GE(fd_in, 0);
    unsigned char buf[100];
    ssize_t n = ::read(fd_in, buf, 100);
    ::close(fd_in);
    ASSERT_EQ(n, 100);

    int fd_out = ::open(truncated.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd_out, 0);
    ::write(fd_out, buf, 100);
    ::close(fd_out);
  }

  // Reading a truncated JPEG should throw an error, not crash
  Sipi::SipiImage img;
  EXPECT_ANY_THROW(img.read(truncated));

  std::remove(truncated.c_str());
}
