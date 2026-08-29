/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <cstdio>

#include "image/SipiImage.h"
#include "image/SipiImageError.h"
#include "image/SipiIO.h"
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
