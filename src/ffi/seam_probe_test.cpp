/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Co-located unit tests (ADR-0003) for the edge-probe seam entries the Rust
// shell calls at the request boundary: sipi_imgroot / sipi_prefix_as_path (read
// the installed engine context) and sipi_image_dims / sipi_mimetype (read a
// validated file). All are sipi_guard-only — no response sink — so they run
// without a transport.

#include "gtest/gtest.h"

#include <climits>
#include <cstdlib>
#include <string>

#include "ffi/engine_context.h"
#include "ffi/sipi_ffi.h"
#include "test_paths.h"

namespace {

const std::string kImagesDir = sipi::test::data_dir() + "/images";

// realpath() the fixture, matching what the real serve path hands the FFI and
// sidestepping the Bazel-runfiles symlink (libmagic would otherwise sniff the
// symlink, not the image behind it) — same helper as serve_image_test.
std::string fixture(const std::string &rel)
{
  const std::string p = kImagesDir + rel;
  char buf[PATH_MAX];
  return realpath(p.c_str(), buf) != nullptr ? std::string(buf) : p;
}

// Install a known engine context so the config getters have something to read.
void install_engine(bool prefix_as_path)
{
  Sipi::ffi::EngineContext eng;// services null (disabled) — the getters don't read them
  eng.imgroot = "/srv/images";
  eng.resolved_imgroot = "/srv/images/resolved";
  eng.prefix_as_path = prefix_as_path;
  Sipi::ffi::set_engine_context(eng);
}

// SipiStrFn that copies the emitted value into a std::string (the ctx).
void collect_str(void *ctx, const char *value) { *static_cast<std::string *>(ctx) = value; }

}// namespace

TEST(SeamProbe, ImgrootReturnsRawThenResolved)
{
  install_engine(true);
  const char *raw = nullptr;
  const char *res = nullptr;
  ASSERT_EQ(sipi_imgroot(0, &raw), 0);
  ASSERT_EQ(sipi_imgroot(1, &res), 0);
  EXPECT_STREQ(raw, "/srv/images");
  EXPECT_STREQ(res, "/srv/images/resolved");
}

TEST(SeamProbe, ImgrootPointerIsProcessStatic)
{
  install_engine(true);
  const char *first = nullptr;
  const char *second = nullptr;
  ASSERT_EQ(sipi_imgroot(0, &first), 0);
  ASSERT_EQ(sipi_imgroot(0, &second), 0);
  // The pointer is into the process-static EngineContext copy — stable, never
  // freed by the caller (the seam contract the Rust shell relies on).
  EXPECT_EQ(first, second);
}

TEST(SeamProbe, PrefixAsPathReflectsConfig)
{
  install_engine(false);
  int v = -1;
  ASSERT_EQ(sipi_prefix_as_path(&v), 0);
  EXPECT_EQ(v, 0);

  install_engine(true);
  ASSERT_EQ(sipi_prefix_as_path(&v), 0);
  EXPECT_EQ(v, 1);
}

TEST(SeamProbe, ImageDimsReadsNativeShape)
{
  const std::string path = fixture("/unit/lena512.tif");
  SipiImageDims dims{};
  ASSERT_EQ(sipi_image_dims(path.c_str(), &dims), 0);
  EXPECT_EQ(dims.width, 512u);
  EXPECT_EQ(dims.height, 512u);
}

TEST(SeamProbe, ImageDimsOnNonImageIsError)
{
  // test.csv exists but is not a recognised image format → read_shape throws →
  // a non-zero (500) status, never a crash or a bogus 0×0 success.
  const std::string path = fixture("/unit/test.csv");
  SipiImageDims dims{};
  EXPECT_NE(sipi_image_dims(path.c_str(), &dims), 0);
}

TEST(SeamProbe, MimetypeMatchesEngineLibmagic)
{
  const std::string tif = fixture("/unit/lena512.tif");
  std::string mime;
  ASSERT_EQ(sipi_mimetype(tif.c_str(), collect_str, &mime), 0);
  EXPECT_NE(mime.find("image/"), std::string::npos) << "got: " << mime;

  const std::string csv = fixture("/unit/test.csv");
  std::string csv_mime;
  ASSERT_EQ(sipi_mimetype(csv.c_str(), collect_str, &csv_mime), 0);
  EXPECT_FALSE(csv_mime.empty());
}
