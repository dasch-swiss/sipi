/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Engine-direct coverage of the IIIF transform pipeline
 * (region -> size -> rotation -> quality -> encode), driven straight
 * against SipiImage without the HTTP server. Mirrors the production
 * order in src/ffi/serve_image.cpp: SipiImage::read() applies the region
 * crop plus the JP2 reduce AND the exact scale (SipiIOJ2k.cpp), then the
 * pipeline rotates, then applies the quality conversion.
 *
 * The JP2 combinations are the ones previously sanitised only at the e2e
 * layer (test/e2e/tests/iiif_compliance.rs, resource_limits.rs): the
 * Kakadu ROI + reduce decode path. Running them here puts that hot path
 * under the fast ASan/UBSan unit job.
 *
 * JP2 tests skip (rather than fail) when the decode throws, matching the
 * "Kakadu may not be available" convention in imginfo_test.cpp; on the
 * linux-x86_64 CI sanitizer runner Kakadu is present and they execute.
 */

#include "gtest/gtest.h"

#include "../../../src/SipiImage.h"
#include "../../../src/SipiImageError.h"
#include "SipiIOTiff.h"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiSize.h"
#include "metadata/icc.h"
#include "test_paths.h"

#include <memory>
#include <string>
#include <sys/stat.h>

namespace {

static const std::string test_images = sipi::test::data_dir() + "/images/";
static const std::string tmp_dir = sipi::test::tmp_dir() + "/";

static const std::string kLena512Jp2 = test_images + "unit/lena512.jp2";
static const std::string kPng16Bit = test_images + "knora/png_16bit.png";
static const std::string kRgbTiff = test_images + "knora/Leaves-small-no-alpha.tif";

std::shared_ptr<Sipi::SipiRegion> region(const std::string &s) { return std::make_shared<Sipi::SipiRegion>(s); }
std::shared_ptr<Sipi::SipiSize> size(const std::string &s) { return std::make_shared<Sipi::SipiSize>(s); }

// Read lena512.jp2 through the Kakadu handler. Returns false (so the test
// can GTEST_SKIP) when the decode is unavailable in this environment.
[[nodiscard]] bool readJp2(Sipi::SipiImage &img,
  const std::shared_ptr<Sipi::SipiRegion> &region_p,
  const std::shared_ptr<Sipi::SipiSize> &size_p,
  bool force_bps_8 = false)
{
  Sipi::SipiIOTiff::initLibrary();
  try {
    img.read(kLena512Jp2, region_p, size_p, force_bps_8);
    return true;
  } catch (const Sipi::SipiImageError &) {
    return false;
  }
}

[[nodiscard]] off_t file_size(const std::string &path)
{
  struct stat buf{};
  return stat(path.c_str(), &buf) == 0 ? buf.st_size : -1;
}

}// namespace

// ── Group A: JP2 ROI + reduce + exact-scale decode ──────────────────────────

TEST(IIIFTransformMatrix, Jp2RoiWithReduceProducesRequestedDims)
{
  Sipi::SipiImage img;
  if (!readJp2(img, region("0,0,256,256"), size("128,"), /*force_bps_8=*/true)) {
    GTEST_SKIP() << "JP2 decode unavailable (Kakadu?)";
  }
  EXPECT_EQ(img.getNx(), 128u);
  EXPECT_EQ(img.getNy(), 128u);

  const std::string out = tmp_dir + "matrix_roi_reduce.jpg";
  EXPECT_NO_THROW(img.write("jpg", out));
  EXPECT_GT(file_size(out), 0);
}

TEST(IIIFTransformMatrix, Jp2RoiWithExactScaleProducesRequestedDims)
{
  Sipi::SipiImage img;
  if (!readJp2(img, region("100,100,300,300"), size("128,128"))) { GTEST_SKIP() << "JP2 decode unavailable (Kakadu?)"; }
  EXPECT_EQ(img.getNx(), 128u);
  EXPECT_EQ(img.getNy(), 128u);
}

TEST(IIIFTransformMatrix, Jp2PctRegionWithExactScale)
{
  Sipi::SipiImage img;
  if (!readJp2(img, region("pct:10,10,80,80"), size("256,256"))) { GTEST_SKIP() << "JP2 decode unavailable (Kakadu?)"; }
  EXPECT_EQ(img.getNx(), 256u);
  EXPECT_EQ(img.getNy(), 256u);
}

TEST(IIIFTransformMatrix, Jp2RegionClampedToImageBounds)
{
  // ROI runs past the right/bottom edge; crop_coords clamps it to the
  // 112x112 remainder (512 - 400). The point is that the clamp arithmetic
  // runs under UBSan and the decode stays in bounds.
  Sipi::SipiImage img;
  if (!readJp2(img, region("400,400,9999,9999"), nullptr)) { GTEST_SKIP() << "JP2 decode unavailable (Kakadu?)"; }
  EXPECT_GT(img.getNx(), 0u);
  EXPECT_GT(img.getNy(), 0u);
  EXPECT_LE(img.getNx(), 112u);
  EXPECT_LE(img.getNy(), 112u);
}

TEST(IIIFTransformMatrix, Jp2ReduceOnlySizes)
{
  Sipi::SipiImage pct;
  if (!readJp2(pct, nullptr, size("pct:50"))) { GTEST_SKIP() << "JP2 decode unavailable (Kakadu?)"; }
  EXPECT_EQ(pct.getNx(), 256u);
  EXPECT_EQ(pct.getNy(), 256u);

  Sipi::SipiImage byh;
  ASSERT_TRUE(readJp2(byh, nullptr, size(",256")));
  EXPECT_EQ(byh.getNx(), 256u);
  EXPECT_EQ(byh.getNy(), 256u);
}

// ── Group B: explicit IIIF rotation / mirror execution ──────────────────────

TEST(IIIFTransformMatrix, RotateBy90TransposesDims)
{
  // Crop to a non-square 200x100 first so the transpose is observable
  // (lena is square).
  Sipi::SipiImage img;
  if (!readJp2(img, region("0,0,200,100"), nullptr)) { GTEST_SKIP() << "JP2 decode unavailable (Kakadu?)"; }
  ASSERT_EQ(img.getNx(), 200u);
  ASSERT_EQ(img.getNy(), 100u);

  ASSERT_TRUE(img.rotate(90.0F, false));
  EXPECT_EQ(img.getNx(), 100u);
  EXPECT_EQ(img.getNy(), 200u);
}

TEST(IIIFTransformMatrix, RotateBy180AndMirrorPreserveDims)
{
  Sipi::SipiImage r180;
  if (!readJp2(r180, region("0,0,200,100"), nullptr)) { GTEST_SKIP() << "JP2 decode unavailable (Kakadu?)"; }
  ASSERT_TRUE(r180.rotate(180.0F, false));
  EXPECT_EQ(r180.getNx(), 200u);
  EXPECT_EQ(r180.getNy(), 100u);

  Sipi::SipiImage mir;
  ASSERT_TRUE(readJp2(mir, region("0,0,200,100"), nullptr));
  ASSERT_TRUE(mir.rotate(0.0F, /*mirror=*/true));
  EXPECT_EQ(mir.getNx(), 200u);
  EXPECT_EQ(mir.getNy(), 100u);
}

TEST(IIIFTransformMatrix, RotateArbitraryAngleGrowsCanvasAndEncodes)
{
  Sipi::SipiImage img;
  if (!readJp2(img, region("0,0,200,100"), nullptr)) { GTEST_SKIP() << "JP2 decode unavailable (Kakadu?)"; }
  ASSERT_TRUE(img.rotate(45.0F, false));
  // A 45-degree rotation expands to the bounding box of the rotated raster.
  EXPECT_GE(img.getNx(), 200u);
  EXPECT_GE(img.getNy(), 100u);

  const std::string out = tmp_dir + "matrix_rotate45.png";
  EXPECT_NO_THROW(img.write("png", out));
  EXPECT_GT(file_size(out), 0);
}

// ── Group C: quality conversions ────────────────────────────────────────────

TEST(IIIFTransformMatrix, BitonalQualityReducesToSingleChannel)
{
  // Use an RGB source so toBitonal exercises the full colour -> gray ->
  // Floyd-Steinberg dither reduction (the UBSan-interesting arithmetic).
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  ASSERT_NO_THROW(img.read(kRgbTiff));
  ASSERT_EQ(img.getNc(), 3u);

  ASSERT_NO_THROW(img.toBitonal());
  EXPECT_EQ(img.getNc(), 1u);

  const std::string out = tmp_dir + "matrix_bitonal.jpg";
  EXPECT_NO_THROW(img.write("jpg", out));
  EXPECT_GT(file_size(out), 0);
}

TEST(IIIFTransformMatrix, GrayQualityConvertsToSingleChannel8Bit)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  ASSERT_NO_THROW(img.read(kRgbTiff));
  ASSERT_EQ(img.getNc(), 3u);

  ASSERT_NO_THROW(img.convertToIcc(Sipi::Icc(Sipi::icc_GRAY_D50), 8));
  EXPECT_EQ(img.getNc(), 1u);
  EXPECT_EQ(img.getBps(), 8u);
}

TEST(IIIFTransformMatrix, SixteenBitPngTo8Bit)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  ASSERT_NO_THROW(img.read(kPng16Bit));
  ASSERT_EQ(img.getBps(), 16u);

  img.to8bps();
  EXPECT_EQ(img.getBps(), 8u);
}

// ── Group D: upscale + clamp dimension arithmetic (UBSan target) ────────────

TEST(IIIFTransformMatrix, Jp2UpscaleByWidth)
{
  Sipi::SipiImage img;
  if (!readJp2(img, nullptr, size("^1024,"))) { GTEST_SKIP() << "JP2 decode unavailable (Kakadu?)"; }
  EXPECT_EQ(img.getNx(), 1024u);
  EXPECT_EQ(img.getNy(), 1024u);
}

TEST(IIIFTransformMatrix, Jp2UpscaleBestFit)
{
  Sipi::SipiImage img;
  if (!readJp2(img, nullptr, size("^!2048,2048"))) { GTEST_SKIP() << "JP2 decode unavailable (Kakadu?)"; }
  EXPECT_EQ(img.getNx(), 2048u);
  EXPECT_EQ(img.getNy(), 2048u);
}

TEST(IIIFTransformMatrix, Jp2UpscaleWithoutCaretRejected)
{
  // Requesting a size larger than the source without the "^" upscale marker
  // is a client error; the size machinery throws SipiSizeError.
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  try {
    img.read(kLena512Jp2, nullptr, size("1024,"));
    FAIL() << "expected SipiSizeError for upscale without '^'";
  } catch (const Sipi::SipiSizeError &) {
    SUCCEED();
  } catch (const Sipi::SipiImageError &) {
    GTEST_SKIP() << "JP2 decode unavailable (Kakadu?)";
  }
}
