/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "error/SipiValueError.h"
#include "format_handlers/output_sink.h"
#include "image/SipiImage.h"
#include "image/SipiImageError.h"
#include "image/SipiIO.h"
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
  ASSERT_TRUE(img.read(src).has_value());
  ASSERT_TRUE(img.write("jpg", dst).has_value());

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
  ASSERT_TRUE(img_low.read(src).has_value());
  ASSERT_TRUE(img_low.write("jpg", dst_low, &params_low).has_value());

  Sipi::SipiImage img_high;
  ASSERT_TRUE(img_high.read(src).has_value());
  ASSERT_TRUE(img_high.write("jpg", dst_high, &params_high).has_value());

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
  ASSERT_TRUE(img_orig.read(src).has_value());
  size_t orig_nx = img_orig.getNx();
  size_t orig_ny = img_orig.getNy();

  ASSERT_TRUE(img_orig.write("jpg", dst).has_value());

  Sipi::SipiImage img_rt;
  ASSERT_TRUE(img_rt.read(dst).has_value());

  EXPECT_EQ(img_rt.getNx(), orig_nx);
  EXPECT_EQ(img_rt.getNy(), orig_ny);

  std::remove(dst.c_str());
}

namespace {
// A CallbackSink ctx that fails every write — simulating a client that
// aborts mid-stream (conn_write_data -> ERREXIT -> longjmp). libjpeg's HTML
// destination manager batches the whole encode into one internal 64KB
// buffer for images this small, so the first (and only) sink write is where
// the abort must land.
struct AlwaysFailCtx
{
  int calls{ 0 };
};

extern "C" int always_fail_write(void *raw_ctx, const uint8_t *, size_t)
{
  auto *ctx = static_cast<AlwaysFailCtx *>(raw_ctx);
  ++ctx->calls;
  return 1;
}
}// namespace

TEST(JpegWrite, ClientAbortMidWriteReturnsClientAbort)
{
  const std::string src = test_images + "unit/palette.tif";
  ASSERT_TRUE(file_exists(src));

  Sipi::SipiImage img;
  ASSERT_TRUE(img.read(src).has_value());
  ASSERT_NE(img.getIcc(), nullptr);
  // The refcount before the aborted write is the real regression check: a
  // longjmp that skips the shared_ptr<Icc>'s destructor leaves it elevated.
  const long icc_refs_before = img.getIcc().use_count();

  AlwaysFailCtx ctx;
  const Sipi::OutputSink sink{ Sipi::CallbackSink{ &always_fail_write, &ctx } };
  const auto result = img.write("jpg", sink);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), Sipi::ErrorCode::kClientAbort);
  EXPECT_GT(ctx.calls, 0);
  EXPECT_EQ(img.getIcc().use_count(), icc_refs_before);
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

  // Reading a truncated JPEG should fail cleanly, not crash. The setjmp
  // landing site funnels every libjpeg decode error into a single error
  // Result (kDecodeFailed) — nothing throws for this fixture.
  Sipi::SipiImage img;
  const auto r = img.read(truncated);
  EXPECT_FALSE(r.has_value());

  std::remove(truncated.c_str());
}
