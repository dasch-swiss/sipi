/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>

#include "error/SipiValueError.h"
#include "format_handlers/output_sink.h"
#include "image/SipiIO.h"
#include "image/SipiImage.h"
#include "image/SipiImageError.h"
#include "test_paths.h"

// `sipi::test::{data_dir,tmp_dir}` honour Bazel's SIPI_TEST_DATA_DIR /
// TEST_TMPDIR envs and fall back to the historical CMake build-tree
// relative paths when unset. See test/test_paths.h.
static const std::string test_images = sipi::test::data_dir() + "/images/";
static const std::string tmp_dir = sipi::test::tmp_dir() + "/";

TEST(PngMetadataRoundTrip, ExifAndIptcSurviveWriteThenRead)
{
  Sipi::SipiImage img;
  ASSERT_TRUE(img.read(test_images + "unit/palette.tif").has_value());
  ASSERT_NE(img.getExif(), nullptr);
  ASSERT_NE(img.getIptc(), nullptr);
  ASSERT_FALSE(img.getExif()->exifBytes().empty());
  ASSERT_FALSE(img.getIptc()->iptcBytes().empty());
  const auto src_exif = img.getExif()->exifBytes();
  const auto src_iptc = img.getIptc()->iptcBytes();

  const std::string out = tmp_dir + "_png_metadata_roundtrip.png";
  ASSERT_TRUE(img.write("png", out).has_value());

  Sipi::SipiImage rt;
  auto r = rt.read(out);
  ASSERT_TRUE(r.has_value()) << "reading back the written PNG failed";
  ASSERT_NE(rt.getExif(), nullptr) << "EXIF did not survive the PNG round-trip";
  ASSERT_NE(rt.getIptc(), nullptr) << "IPTC did not survive the PNG round-trip";
  EXPECT_EQ(rt.getExif()->exifBytes(), src_exif);
  EXPECT_EQ(rt.getIptc()->iptcBytes(), src_iptc);

  std::remove(out.c_str());
}

namespace {
// A CallbackSink ctx that accepts the first write and fails every one after —
// simulating a client that aborts mid-stream (conn_write_data → png_error →
// longjmp).
struct AbortAfterFirstCtx
{
  int calls{ 0 };
};

extern "C" int abort_after_first_write(void *raw_ctx, const uint8_t *, size_t)
{
  auto *ctx = static_cast<AbortAfterFirstCtx *>(raw_ctx);
  return (++ctx->calls > 1) ? 1 : 0;
}
}// namespace

TEST(PngMetadataRoundTrip, ClientAbortMidWriteReturnsClientAbort)
{
  Sipi::SipiImage img;
  ASSERT_TRUE(img.read(test_images + "unit/palette.tif").has_value());
  ASSERT_NE(img.getIcc(), nullptr);
  // The refcount before the aborted write is the real regression check: a
  // longjmp that skips the shared_ptr<Icc>'s destructor leaves it elevated.
  const long icc_refs_before = img.getIcc().use_count();

  AbortAfterFirstCtx ctx;
  const Sipi::OutputSink sink{ Sipi::CallbackSink{ &abort_after_first_write, &ctx } };
  const auto result = img.write("png", sink);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), Sipi::ErrorCode::kClientAbort);
  EXPECT_EQ(img.getIcc().use_count(), icc_refs_before);
}
