/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * JPEG 2000 decode of subsampled components. Fixtures: the ISO/IEC 15444-4
 * files file2.jp2 (sYCC) and file3.jp2 (the same image, chroma subsampled
 * 2x2), and a file3.jp2 copy with a patched SIZ marker
 * (test/_test_data/images/malformed/README.md).
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "error/SipiValueError.h"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiSize.h"
#include "image/SipiImage.h"
#include "image_processing/processing.h"
#include "test_paths.h"

namespace {

using Sipi::ErrorCode;
using Sipi::SipiImage;
using Sipi::SipiRegion;
using Sipi::SipiSize;

const std::string kConformance = sipi::test::data_dir() + "/images/iso-15444-4";
const std::string kFile2 = kConformance + "/testfiles_jp2/file2.jp2";
const std::string kFile3 = kConformance + "/testfiles_jp2/file3.jp2";
const std::string kUnsupportedSubsampling =
  sipi::test::data_dir() + "/images/malformed/j2k_subsampling_unsupported.jp2";

// Mean absolute difference of the decode (co-sited sample replication)
// against the conformance reference, as measured when the decode path landed;
// the test allows this plus 0.5.
constexpr double kFile3ReferenceMeanAbs = 0.4945;

double psnr(const SipiImage &a, const SipiImage &b)
{
  const auto pa = a.pixels_view();
  const auto pb = b.pixels_view();
  double sum = 0.0;
  for (std::size_t i = 0; i < pa.size(); ++i) {
    const double d = static_cast<double>(pa[i]) - static_cast<double>(pb[i]);
    sum += d * d;
  }
  const double mse = sum / static_cast<double>(pa.size());
  return 10.0 * std::log10(255.0 * 255.0 / mse);
}

void expect_same_shape(const SipiImage &a, const SipiImage &b)
{
  EXPECT_EQ(a.getNx(), b.getNx());
  EXPECT_EQ(a.getNy(), b.getNy());
  EXPECT_EQ(a.getNc(), b.getNc());
  EXPECT_EQ(a.getBps(), b.getBps());
}

TEST(J2kSubsampling, FullDecodeHasComponentZeroGeometry)
{
  SipiImage img;
  ASSERT_TRUE(img.read(kFile3).has_value());
  EXPECT_EQ(img.getNx(), 480u);
  EXPECT_EQ(img.getNy(), 640u);
  EXPECT_EQ(img.getNc(), 3u);
}

TEST(J2kSubsampling, MatchesConformanceReference)
{
  SipiImage decoded;
  ASSERT_TRUE(decoded.read(kFile3).has_value());
  SipiImage reference;
  ASSERT_TRUE(reference.read(kConformance + "/reference_jp2/jp2_3.tif").has_value());
  expect_same_shape(decoded, reference);
  ASSERT_EQ(decoded.getPhoto(), reference.getPhoto());

  const auto delta = Sipi::processing::maxPixelDelta(decoded, reference);
  ASSERT_TRUE(delta.has_value());
  const double p = psnr(decoded, reference);
  EXPECT_LE(delta->mean_abs, kFile3ReferenceMeanAbs + 0.5);
  EXPECT_GE(p, 30.0);
}

TEST(J2kSubsampling, ReducedResolutionRead)
{
  // 240 px wide is a reduce by one level; file2.jp2 has the same geometry
  // without subsampling, so its decode gives the expected dimensions.
  SipiImage sub;
  ASSERT_TRUE(sub.read(kFile3, nullptr, std::make_shared<SipiSize>("240,")).has_value());
  SipiImage plain;
  ASSERT_TRUE(plain.read(kFile2, nullptr, std::make_shared<SipiSize>("240,")).has_value());
  expect_same_shape(sub, plain);
  EXPECT_EQ(sub.getNx(), 240u);
}

struct RegionCase
{
  const char *name;
  int x, y;
  std::size_t w, h;
  const char *size;// nullptr: full size
};

class J2kSubsamplingRegion : public ::testing::TestWithParam<RegionCase>
{
};

// A region read decodes a window expanded to whole subsampled samples and
// crops back: it must have the dimensions file2.jp2 gives for the same request
// and, at full size, the pixels of the full decode cropped to the region.
TEST_P(J2kSubsamplingRegion, MatchesFullDecodeCrop)
{
  const RegionCase &rc = GetParam();
  const auto region = std::make_shared<SipiRegion>(rc.x, rc.y, rc.w, rc.h);
  const auto size = rc.size == nullptr ? nullptr : std::make_shared<SipiSize>(rc.size);

  SipiImage sub;
  const auto result = sub.read(kFile3, region, size);
  ASSERT_TRUE(result.has_value()) << result.error().diagnostic_message();
  SipiImage plain;
  ASSERT_TRUE(plain.read(kFile2, region, size).has_value());
  expect_same_shape(sub, plain);

  if (rc.size == nullptr) {
    EXPECT_EQ(sub.getNx(), rc.w);
    EXPECT_EQ(sub.getNy(), rc.h);
    SipiImage full;
    ASSERT_TRUE(full.read(kFile3).has_value());
    ASSERT_TRUE(Sipi::processing::crop(full, rc.x, rc.y, rc.w, rc.h).has_value());
    const auto delta = Sipi::processing::maxPixelDelta(sub, full);
    ASSERT_TRUE(delta.has_value());
    EXPECT_EQ(delta->max_abs, 0);
  }
}

INSTANTIATE_TEST_SUITE_P(OddRegions,
  J2kSubsamplingRegion,
  ::testing::Values(RegionCase{ "odd_origin_odd_extent", 33, 47, 101, 77, nullptr },
    RegionCase{ "one_by_one_odd_origin", 33, 47, 1, 1, nullptr },
    RegionCase{ "one_by_n_odd_origin", 33, 47, 1, 50, nullptr },
    RegionCase{ "odd_region_reduced", 33, 47, 101, 77, "pct:50" },
    RegionCase{ "small_odd_region_reduced", 33, 47, 3, 3, "pct:50" }),
  [](const ::testing::TestParamInfo<RegionCase> &info) { return std::string{ info.param.name }; });

// The guard fires before decompressor.start, so the read returns at once. It
// runs on a detached thread so a regression (a spinning decode) fails the
// test after 5 s instead of hanging formats_test; std::async would block in
// the future's destructor.
TEST(J2kSubsampling, UnsupportedFactorFailsFast)
{
  auto promise = std::make_shared<std::promise<std::optional<ErrorCode>>>();
  auto future = promise->get_future();
  std::thread([promise] {
    SipiImage img;
    const auto result = img.read(kUnsupportedSubsampling);
    promise->set_value(result.has_value() ? std::nullopt : std::optional{ result.error().code() });
  }).detach();

  ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  EXPECT_EQ(future.get(), ErrorCode::kUnsupportedFormat);
}

}// namespace
