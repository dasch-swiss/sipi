/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gtest/gtest.h"

#include "SipiImage.h"
#include "SipiImageError.h"
#include "error/SipiValueError.h"
#include "image_processing/processing.h"
#include "format_handlers/SipiIOTiff.h"
#include "observability/metrics.h"
#include "test_paths.h"
#include <cmath>
#include <filesystem>
#include <ranges>
#include <sys/stat.h>

// See test/test_paths.h for the data_dir/tmp_dir env-var contract.
static const std::string test_images = sipi::test::data_dir() + "/images/";
static const std::string tmp_dir = sipi::test::tmp_dir() + "/";

// small function to check if file exist
inline bool exists_file(const std::string &name)
{
  struct stat buffer
  {
  };
  return (stat(name.c_str(), &buffer) == 0);
}

inline bool image_identical(const std::string &name1, const std::string &name2)
{
  Sipi::SipiImage img1;
  Sipi::SipiImage img2;
  EXPECT_TRUE(img1.read(name1).has_value());
  EXPECT_TRUE(img2.read(name2).has_value());

  return (img1 == img2);
}

const std::string leavesSmallWithAlpha = test_images + "knora/Leaves-small-alpha.tif";
const std::string leavesSmallNoAlpha = test_images + "knora/Leaves-small-no-alpha.tif";
const std::string png16bit = test_images + "knora/png_16bit.png";
const std::string pngPaletteAlpha = test_images + "unit/mario.png";
const std::string leaves8tif = test_images + "knora/Leaves8.tif";
const std::string cielab = test_images + "unit/cielab.tif";
const std::string cielab16 = test_images + "unit/CIELab16.tif";
const std::string palette = test_images + "unit/palette.tif";
const std::string wrongrotation = test_images + "unit/image_orientation.jpg";
const std::string watermark_correct = test_images + "unit/watermark_correct.tif";
const std::string watermark_incorrect = test_images + "unit/watermark_incorrect.tif";
const std::string tiffJpegScanlineBug = test_images + "knora/tiffJpegScanlineBug.tif";
// Tiled multi-IFD pyramid TIFF (levels 512/256/128/64, tiled 256²) used to
// exercise TIFF pyramid-level selection.
const std::string lena512Pyramid = test_images + "unit/lena512_pyramid.tif";
const std::string imgExifGps = test_images + "unit/img_exif_gps.jpg";
const std::string imgExifGpsTifOut = tmp_dir + "img_exif_gps_out.tif";

// Check if configuration file can be found
TEST(SipiImage, CheckIfTestImagesCanBeFound)
{
  EXPECT_TRUE(exists_file(leavesSmallWithAlpha));
  EXPECT_TRUE(exists_file(leavesSmallNoAlpha));
  EXPECT_TRUE(exists_file(png16bit));
  EXPECT_TRUE(exists_file(pngPaletteAlpha));
  EXPECT_TRUE(exists_file(leaves8tif));
  EXPECT_TRUE(exists_file(cielab));
  EXPECT_TRUE(exists_file(cielab16));
  EXPECT_TRUE(exists_file(palette));
  EXPECT_TRUE(exists_file(wrongrotation));
  EXPECT_TRUE(exists_file(watermark_correct));
  EXPECT_TRUE(exists_file(watermark_incorrect));
  EXPECT_TRUE(exists_file(tiffJpegScanlineBug));
  EXPECT_TRUE(exists_file(lena512Pyramid));
}

// The pyramid fixture reads correctly at full resolution and at a reduced size,
// and a reduced request decodes from a deeper pyramid level. Output dimensions
// are level-independent (the pipeline always yields the requested size), so the
// reduced-level decode is verified via the reduced-decodes counter rather than
// dims: a full-size read must NOT bump it (level 0), a reduced read must.
TEST(SipiImage, PyramidTiffReadsFullAndReduced)
{
  auto &reduced_decodes = Sipi::observability::Metrics::instance().tiff_pyramid_reduced_decodes_total;
  Sipi::SipiIOTiff::initLibrary();

  // A full read decodes from level 0 — the reduced-decodes counter stays put.
  const double before_full = reduced_decodes.Value();
  Sipi::SipiImage full;
  ASSERT_TRUE(full.read(lena512Pyramid).has_value());
  EXPECT_EQ(full.getNx(), 512u);
  EXPECT_EQ(full.getNy(), 512u);
  EXPECT_DOUBLE_EQ(reduced_decodes.Value(), before_full);

  // A !128,128 best-fit of the 512² source reduces by /4 → a deeper pyramid
  // level, so the reduced-decodes counter advances by one.
  const double before_reduced = reduced_decodes.Value();
  const std::shared_ptr<Sipi::SipiRegion> region;
  const auto size = std::make_shared<Sipi::SipiSize>("!128,128");
  Sipi::SipiImage reduced;
  ASSERT_TRUE(reduced.read(lena512Pyramid, region, size).has_value());
  EXPECT_EQ(reduced.getNx(), 128u);
  EXPECT_EQ(reduced.getNy(), 128u);
  EXPECT_DOUBLE_EQ(reduced_decodes.Value(), before_reduced + 1.0);
}

TEST(SipiImage, ImageComparison)
{
  Sipi::SipiIOTiff::initLibrary();
  EXPECT_TRUE(image_identical(leaves8tif, leaves8tif));
}

// Convert Tiff with alpha channel to JPG
TEST(SipiImage, ConvertTiffWithAlphaToJPG)
{
  Sipi::SipiIOTiff::initLibrary();
  const std::shared_ptr<Sipi::SipiRegion> region;
  const auto size = std::make_shared<Sipi::SipiSize>("!128,128");

  Sipi::SipiImage img;

  EXPECT_TRUE(img.read(leavesSmallWithAlpha, region, size).has_value());

  ASSERT_TRUE(img.write("jpg", tmp_dir + "Leaves-small-with-alpha.jpg").has_value());
}

// Convert Tiff with no alpha channel to JPG
TEST(SipiImage, ConvertTiffWithNoAlphaToJPG)
{
  Sipi::SipiIOTiff::initLibrary();
  const std::shared_ptr<Sipi::SipiRegion> region;
  const auto size = std::make_shared<Sipi::SipiSize>("!128,128");

  Sipi::SipiImage img;

  EXPECT_TRUE(img.read(leavesSmallNoAlpha, region, size).has_value());

  ASSERT_TRUE(img.write("jpg", tmp_dir + "Leaves-small-no-alpha.jpg").has_value());
}

// Convert PNG 16 bit with alpha channel and ICC profile to TIFF and back
TEST(SipiImage, ConvertPng16BitToJpxToPng)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img1;
  ASSERT_TRUE(img1.read(png16bit).has_value());
  ASSERT_TRUE(img1.write("tif", tmp_dir + "png_16bit.tif").has_value());

  Sipi::SipiImage img2;
  ASSERT_TRUE(img2.read(tmp_dir + "png_16bit.tif").has_value());
  ASSERT_TRUE(img2.write("png", tmp_dir + "png_16bit_X.png").has_value());
  // EXPECT_TRUE(image_identical(png16bit, tmp_dir + "png_16bit_X.png"));
}

// Convert PNG 16 bit with alpha channel and ICC profile to JPX
TEST(SipiImage, ConvertPng16BitToJpx)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img1;

  ASSERT_TRUE(img1.read(png16bit).has_value());

  ASSERT_TRUE(img1.write("jpx", tmp_dir + "png_16bit.jpx").has_value());

  EXPECT_TRUE(image_identical(png16bit, tmp_dir + "png_16bit.jpx"));
}


// Convert PNG 16 bit with alpha channel and ICC profile to TIFF
TEST(SipiImage, ConvertPng16BitToTiff)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img1;

  ASSERT_TRUE(img1.read(png16bit).has_value());

  ASSERT_TRUE(img1.write("tif", tmp_dir + "png_16bit.tif").has_value());

  EXPECT_TRUE(image_identical(png16bit, tmp_dir + "png_16bit.tif"));
}

// Convert PNG 16 bit with alpha channel and ICC profile to JPEG
TEST(SipiImage, ConvertPng16BitToJpg)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img1;

  ASSERT_TRUE(img1.read(png16bit).has_value());

  ASSERT_TRUE(img1.write("jpg", tmp_dir + "png_16bit.jpg").has_value());
}

TEST(SipiImage, ConvertPNGPaletteAlphaToTiff)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img1;

  ASSERT_TRUE(img1.read(pngPaletteAlpha).has_value());
  ASSERT_TRUE(img1.write("tif", tmp_dir + "_mario.tif").has_value());
  EXPECT_TRUE(image_identical(test_images + "unit/mario.tif", tmp_dir + "_mario.tif"));
}

TEST(SipiImage, CIELab_Conversion)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img1;
  Sipi::SipiImage img2;
  Sipi::SipiImage img3;

  ASSERT_TRUE(img1.read(cielab).has_value());
  ASSERT_TRUE(img1.write("jpx", tmp_dir + "_cielab.jpx").has_value());
  ASSERT_TRUE(img2.read(tmp_dir + "_cielab.jpx").has_value());
  ASSERT_TRUE(img2.write("tif", tmp_dir + "_cielab.tif").has_value());

  // now test if conversion back to TIFF gives an identical image
  EXPECT_TRUE(image_identical(cielab, tmp_dir + "_cielab.tif"));
  ASSERT_TRUE(img3.read(tmp_dir + "_cielab.jpx").has_value());
  ASSERT_TRUE(img3.write("png", tmp_dir + "_cielab.png").has_value());
}

TEST(SipiImage, CIELab16_Conversion)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img1;
  Sipi::SipiImage img2;
  Sipi::SipiImage img3;
  Sipi::SipiImage img4;

  ASSERT_TRUE(img1.read(cielab16).has_value());
  ASSERT_TRUE(img1.write("jpx", tmp_dir + "_CIELab16.jpx").has_value());
  ASSERT_TRUE(img2.read(tmp_dir + "_CIELab16.jpx").has_value());
  ASSERT_TRUE(img2.write("tif", tmp_dir + "_CIELab.tif").has_value());

  // now test if conversion back to TIFF gives an identical image
  EXPECT_TRUE(image_identical(cielab16, tmp_dir + "_CIELab.tif"));
  ASSERT_TRUE(img3.read(tmp_dir + "_CIELab16.jpx").has_value());
  ASSERT_TRUE(img3.write("png", tmp_dir + "_CIELab16.png").has_value());
  ASSERT_TRUE(img4.read(tmp_dir + "_CIELab16.jpx").has_value());
  ASSERT_TRUE(img4.write("jpg", tmp_dir + "_CIELab16.jpg").has_value());
}

TEST(SipiImage, CMYK_Conversion)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img1;
  Sipi::SipiImage img2;
  Sipi::SipiImage img3;
  Sipi::SipiImage img4;

  const std::string cmyk = test_images + "unit/cmyk.tif";
  EXPECT_TRUE(exists_file(cmyk));

  ASSERT_TRUE(img1.read(cmyk).has_value());
  ASSERT_TRUE(img1.write("jpx", tmp_dir + "_cmyk.jpx").has_value());
  ASSERT_TRUE(img2.read(tmp_dir + "_cmyk.jpx").has_value());
  ASSERT_TRUE(img2.write("tif", tmp_dir + "_cmyk_2.tif").has_value());

  // now test if conversion back to TIFF gives an identical image
  EXPECT_TRUE(image_identical(cmyk, tmp_dir + "_cmyk_2.tif"));
  ASSERT_TRUE(img3.read(tmp_dir + "_cmyk.jpx").has_value());
  ASSERT_TRUE(img3.write("png", tmp_dir + "_cmyk.png").has_value());
  ASSERT_TRUE(img4.read(tmp_dir + "_cmyk.jpx").has_value());
  ASSERT_TRUE(img4.write("jpg", tmp_dir + "_cmyk.jpg").has_value());
}

TEST(SipiImage, PALETTE_Conversion)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img1;
  ASSERT_TRUE(img1.read(palette).has_value());
  ASSERT_TRUE(img1.write("jpx", tmp_dir + "_palette.jpx").has_value());
}

TEST(SipiImage, GRAYICC_Conversion_01)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img1;
  Sipi::SipiImage img2;
  Sipi::SipiImage img3;

  const std::string grayicc_jp2 = test_images + "unit/gray_with_icc.jp2";
  const std::string grayicc_jp2_to_jpeg = tmp_dir + "_gray_with_icc.jpg";

  EXPECT_TRUE(exists_file(grayicc_jp2));

  // read from jp2 and write to jpeg
  ASSERT_TRUE(img1.read(grayicc_jp2).has_value());
  ASSERT_TRUE(img1.write("jpg", grayicc_jp2_to_jpeg).has_value());
  ASSERT_TRUE(img2.read(grayicc_jp2_to_jpeg).has_value());
}

TEST(SipiImage, GRAYICC_Conversion_02)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img1;
  Sipi::SipiImage img2;
  Sipi::SipiImage img3;

  const std::string gray_icc_another_jpeg = test_images + "unit/gray_with_icc_another.jpg";
  const std::string gray_icc_another_jpeg_to_jp2 = tmp_dir + "_gray_with_icc_another.jpx";

  EXPECT_TRUE(exists_file(gray_icc_another_jpeg));

  // read from jpeg and write to jp2
  ASSERT_TRUE(img1.read(gray_icc_another_jpeg).has_value());
  ASSERT_TRUE(img1.write("jpx", gray_icc_another_jpeg_to_jp2).has_value());
  ASSERT_TRUE(img2.read(gray_icc_another_jpeg_to_jp2).has_value());
}

TEST(SipiImage, CMYK_lossy_compression)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  const std::shared_ptr<Sipi::SipiRegion> region = nullptr;
  const std::shared_ptr<Sipi::SipiSize> size = nullptr;

  const std::string cmyk = test_images + "unit/cmyk.tif";
  EXPECT_TRUE(exists_file(cmyk));

  ASSERT_TRUE(img.readSource(cmyk, region, size).has_value());
  Sipi::SipiCompressionParams params = {
    { Sipi::J2K_rates, "0.5 0.2 0.1 0.025" }, { Sipi::J2K_Clayers, "4" }, { Sipi::J2K_Clevels, "3" }
  };
  ASSERT_TRUE(img.write("jpx", tmp_dir + "_cmyk_lossy.jp2", &params).has_value());
  EXPECT_TRUE(image_identical(test_images + "unit/cmyk_lossy.jp2", tmp_dir + "_cmyk_lossy.jp2"));
}

TEST(SipiImage, WrongRotation)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  const std::shared_ptr<Sipi::SipiRegion> region = nullptr;
  const std::shared_ptr<Sipi::SipiSize> size = nullptr;
  ASSERT_TRUE(img.readSource(wrongrotation, region, size).has_value());
  // EXPECT_EQ(img.getNx(), 3264);
  // EXPECT_EQ(img.getNy(), 2448);
  // EXPECT_EQ(img.getNc(), 3);
  EXPECT_EQ(img.getOrientation(), Sipi::RIGHTTOP);
  EXPECT_TRUE(Sipi::processing::set_topleft(img).has_value());
  // EXPECT_EQ(img.getNx(), 2448);
  // EXPECT_EQ(img.getNy(), 3264);
  // EXPECT_EQ(img.getNc(), 3);
  // EXPECT_EQ(img.getOrientation(), Sipi::TOPLEFT);
  ASSERT_TRUE(img.write("tif", tmp_dir + "_image_orientation.tif").has_value());
  EXPECT_TRUE(image_identical(test_images + "unit/image_orientation.tif", tmp_dir + "_image_orientation.tif"));
}

// Apply watermark to image
TEST(SipiImage, Watermark)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img1;
  Sipi::SipiImage img2;
  Sipi::SipiImage img3;
  Sipi::SipiImage img4;

  std::string maori = test_images + "unit/MaoriFigure.jpg";
  std::string gradstars = test_images + "unit/gradient-stars.tif";
  std::string maoriWater = test_images + "unit/MaoriFigureWatermark.jpg";

  EXPECT_TRUE(exists_file(maori));
  EXPECT_TRUE(exists_file(gradstars));

  ASSERT_TRUE(img1.read(cielab).has_value());
  EXPECT_TRUE(Sipi::processing::add_watermark(img1, watermark_correct).has_value());

  Sipi::SipiImage cielab16_reference;
  ASSERT_TRUE(cielab16_reference.read(cielab16).has_value());
  ASSERT_TRUE(img2.read(cielab16).has_value());
  EXPECT_TRUE(Sipi::processing::add_watermark(img2, watermark_correct).has_value());

  // Watermarking must actually change the pixels (bps == 16 must not be a silent no-op),
  // while leaving the image geometry untouched.
  ASSERT_EQ(img2.getNx(), cielab16_reference.getNx());
  ASSERT_EQ(img2.getNy(), cielab16_reference.getNy());
  ASSERT_EQ(img2.getNc(), cielab16_reference.getNc());
  ASSERT_EQ(img2.getBps(), cielab16_reference.getBps());
  {
    const auto watermarked_bytes = img2.pixels_view();
    const auto reference_bytes = cielab16_reference.pixels_view();
    ASSERT_EQ(watermarked_bytes.size(), reference_bytes.size());
    const Sipi::word *watermarked = reinterpret_cast<const Sipi::word *>(watermarked_bytes.data());
    const Sipi::word *reference = reinterpret_cast<const Sipi::word *>(reference_bytes.data());
    const size_t nwords = watermarked_bytes.size() / sizeof(Sipi::word);

    bool differs = false;
    int max_abs_diff = 0;
    for (size_t idx = 0; idx < nwords; idx++) {
      const int diff = static_cast<int>(watermarked[idx]) - static_cast<int>(reference[idx]);
      if (diff != 0) differs = true;
      const int abs_diff = diff < 0 ? -diff : diff;
      if (abs_diff > max_abs_diff) max_abs_diff = abs_diff;
    }

    EXPECT_TRUE(differs);
    EXPECT_GT(max_abs_diff, 0);
    EXPECT_LT(max_abs_diff, 65535 / 2);
  }

  ASSERT_TRUE(img3.read(maori).has_value());

  EXPECT_TRUE(Sipi::processing::add_watermark(img3, gradstars).has_value());
  /* ASSERT_NO_THROW(img3.write("jpg", maoriWater)); */

  ASSERT_TRUE(img4.read(maoriWater).has_value());
  EXPECT_TRUE(Sipi::processing::compare(img4, img3).value_or(1000) < 0.007);// 0.00605

  ASSERT_TRUE(img3.read(maori).has_value());
  EXPECT_TRUE(Sipi::processing::compare(img4, img3) > 0.017);// 0.0174

  EXPECT_TRUE(Sipi::processing::rotate(img3, 90).has_value());
}

TEST(SipiImage, CMYK_With_Alpha_Conversion)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img1;
  Sipi::SipiImage img2;

  const std::string tif_cmyk_with_alpha = test_images + "unit/cmyk_with_alpha.tif";
  const std::string tif_cmyk_with_alpha_converted_to_jpx = tmp_dir + "cmyk_with_alpha.jpx";
  const std::string tif_cmyk_with_alpha_converted_from_jpx_to_tif = tmp_dir + "cmyk_with_alpha_.tif";
  const std::string tif_cmyk_with_alpha_converted_to_jpg = tmp_dir + "cmyk_with_alpha_.jpg";
  const std::string tif_cmyk_with_alpha_converted_to_png = tmp_dir + "cmyk_with_alpha_.png";

  ASSERT_TRUE(img1.read(tif_cmyk_with_alpha).has_value());
  ASSERT_TRUE(img1.write("jpx", tif_cmyk_with_alpha_converted_to_jpx).has_value());
  ASSERT_TRUE(img2.read(tif_cmyk_with_alpha_converted_to_jpx).has_value());

  // now test if conversion back to TIFF gives an identical image
  ASSERT_TRUE(img2.write("tif", tif_cmyk_with_alpha_converted_from_jpx_to_tif).has_value());
  EXPECT_TRUE(image_identical(tif_cmyk_with_alpha, tif_cmyk_with_alpha_converted_from_jpx_to_tif));

  // now test if conversion to JPG is working
  ASSERT_TRUE(img2.write("jpg", tif_cmyk_with_alpha_converted_to_jpg).has_value());

  // now test if conversion to PNG is working
  ASSERT_TRUE(img2.write("png", tif_cmyk_with_alpha_converted_to_png).has_value());
}

// Convert TIFF with JPEG compression and automatic YCbCr conversion via
// TIFFTAG_JPEGCOLORMODE = JPEGCOLORMODE_RGB.
TEST(SipiImage, TiffJpegAutoRgbConvert)
{
  Sipi::SipiIOTiff::initLibrary();

  Sipi::SipiImage img;

  EXPECT_TRUE(img.read(tiffJpegScanlineBug).has_value());
  ASSERT_TRUE(img.write("jpx", tmp_dir + "tiffJpegScanlineBug.jp2").has_value());
}

double errorPercent(double actual, double expected) { return abs((actual - expected) / expected); }

void assertErrorPercent(double actual, double expected, double lessThan)
{
  auto p = errorPercent(actual, expected);
  EXPECT_TRUE(p < lessThan);
}

TEST(SipiImage, TiffPyramidLayers)
{
  Sipi::SipiIOTiff::initLibrary();

  const int x = 4032, y = 3024;

  // The pyramid TIFF is identical for every reduce level (same source, same
  // params), so build it once and read it back at each percentage below.
  Sipi::SipiCompressionParams params = { { Sipi::TIFF_Pyramid, "yes" } };
  Sipi::SipiImage src;
  EXPECT_TRUE(src.read(imgExifGps).has_value());
  EXPECT_TRUE(src.getNx() == x);
  EXPECT_TRUE(src.getNy() == y);
  ASSERT_TRUE(src.write("tif", imgExifGpsTifOut, &params).has_value());

  const std::shared_ptr<Sipi::SipiRegion> region;
  for (int step : std::views::iota(0, 5)) {
    const int reduce = 100 - (step * 10);
    const auto size = std::make_shared<Sipi::SipiSize>("pct:" + std::to_string(reduce));

    Sipi::SipiImage img;
    EXPECT_TRUE(img.read(imgExifGpsTifOut, region, size).has_value());

    assertErrorPercent(img.getNx(), static_cast<float>(x) * reduce / 100, 0.01);
    assertErrorPercent(img.getNy(), static_cast<float>(y) * reduce / 100, 0.01);
  }
}

TEST(SipiImage, PercentParsing)
{
  Sipi::SipiIOTiff::initLibrary();
  const std::shared_ptr<Sipi::SipiRegion> region;
  const auto size = std::make_shared<Sipi::SipiSize>("pct:0");

  Sipi::SipiImage img;
  EXPECT_TRUE(img.read(leavesSmallWithAlpha, region, size).has_value());
}

// ================================================================
// Constructor error path tests — verify enriched error messages
// ================================================================

TEST(SipiImage, ConstructorRgbChannelMismatchIncludesChannelCount)
{
  // RGB expects 3 or 4 channels; passing 1 should throw with the channel count in the message
  try {
    Sipi::SipiImage img(100, 100, 1, 8, Sipi::PhotometricInterpretation::RGB);
    FAIL() << "Expected SipiImageError for RGB with 1 channel";
  } catch (const Sipi::SipiImageError &e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("1 channels"), std::string::npos) << "Error should include channel count, got: " << msg;
    EXPECT_NE(msg.find("RGB"), std::string::npos) << "Error should mention RGB, got: " << msg;
  }
}

TEST(SipiImage, ConstructorRgbWith5ChannelsThrows)
{
  try {
    Sipi::SipiImage img(50, 50, 5, 8, Sipi::PhotometricInterpretation::RGB);
    FAIL() << "Expected SipiImageError for RGB with 5 channels";
  } catch (const Sipi::SipiImageError &e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("5 channels"), std::string::npos) << "Error should include channel count, got: " << msg;
  }
}

TEST(SipiImage, ConstructorUnsupportedBpsIncludesBpsValue)
{
  // Only 8 and 16 bps are supported; passing 32 should throw with the bps value
  try {
    Sipi::SipiImage img(100, 100, 3, 32, Sipi::PhotometricInterpretation::RGB);
    FAIL() << "Expected SipiImageError for unsupported bps=32";
  } catch (const Sipi::SipiImageError &e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("32"), std::string::npos) << "Error should include bps value, got: " << msg;
    EXPECT_NE(msg.find("8 and 16"), std::string::npos) << "Error should mention supported values, got: " << msg;
  }
}

TEST(SipiImage, ConstructorUnsupportedBps4Throws)
{
  try {
    Sipi::SipiImage img(100, 100, 3, 4, Sipi::PhotometricInterpretation::RGB);
    FAIL() << "Expected SipiImageError for unsupported bps=4";
  } catch (const Sipi::SipiImageError &e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("4"), std::string::npos) << "Error should include bps value, got: " << msg;
  }
}

TEST(SipiImage, ConstructorZeroDimensionsIncludesDimensions)
{
  // Zero width should result in zero buffer → throw with dimensions
  try {
    Sipi::SipiImage img(0, 100, 3, 8, Sipi::PhotometricInterpretation::RGB);
    FAIL() << "Expected SipiImageError for zero-width image";
  } catch (const Sipi::SipiImageError &e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("0x100"), std::string::npos) << "Error should include dimensions, got: " << msg;
  }
}

TEST(SipiImage, ConstructorZeroHeightIncludesDimensions)
{
  try {
    Sipi::SipiImage img(100, 0, 3, 8, Sipi::PhotometricInterpretation::RGB);
    FAIL() << "Expected SipiImageError for zero-height image";
  } catch (const Sipi::SipiImageError &e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("100x0"), std::string::npos) << "Error should include dimensions, got: " << msg;
  }
}

TEST(SipiImage, ConstructorGrayscaleChannelMismatchThrows)
{
  // MINISBLACK expects 1 or 2 channels; 3 should throw
  try {
    Sipi::SipiImage img(100, 100, 3, 8, Sipi::PhotometricInterpretation::MINISBLACK);
    FAIL() << "Expected SipiImageError for MINISBLACK with 3 channels";
  } catch (const Sipi::SipiImageError &e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("Mismatch"), std::string::npos) << "Error should mention mismatch, got: " << msg;
  }
}

TEST(SipiImage, ConstructorValidRgb8bpsSucceeds)
{
  // Valid construction should not throw
  EXPECT_NO_THROW(Sipi::SipiImage(100, 100, 3, 8, Sipi::PhotometricInterpretation::RGB));
}

TEST(SipiImage, ConstructorValidRgba16bpsSucceeds)
{
  EXPECT_NO_THROW(Sipi::SipiImage(50, 50, 4, 16, Sipi::PhotometricInterpretation::RGB));
}

TEST(SipiImage, ConstructorValidGrayscaleSucceeds)
{
  EXPECT_NO_THROW(Sipi::SipiImage(200, 200, 1, 8, Sipi::PhotometricInterpretation::MINISBLACK));
}

TEST(SipiImage, GetIccReturnsNullForNewImage)
{
  Sipi::SipiImage img(10, 10, 3, 8, Sipi::PhotometricInterpretation::RGB);
  EXPECT_EQ(img.getIcc(), nullptr) << "Newly constructed image should have no ICC profile";
}

// ================================================================
// Boundary-condition regression tests for bilinn() / scaling segfault
// ================================================================

TEST(SipiImage, ScaleBoundaryDoesNotCrash)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  const std::shared_ptr<Sipi::SipiRegion> region;
  const auto size = std::make_shared<Sipi::SipiSize>("2,2");
  EXPECT_TRUE(img.read(leavesSmallNoAlpha, region, size).has_value());
  EXPECT_EQ(img.getNx(), 2u);
  EXPECT_EQ(img.getNy(), 2u);
}

TEST(SipiImage, ScaleToSmallPercentDoesNotCrash)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  const std::shared_ptr<Sipi::SipiRegion> region;
  const auto size = std::make_shared<Sipi::SipiSize>("pct:1");
  EXPECT_TRUE(img.read(leavesSmallNoAlpha, region, size).has_value());
}

TEST(SipiImage, ScaleUpDoesNotCrash)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  const std::shared_ptr<Sipi::SipiRegion> region;
  const auto size = std::make_shared<Sipi::SipiSize>("^1000,1000");
  EXPECT_TRUE(img.read(leavesSmallNoAlpha, region, size).has_value());
}

TEST(SipiImage, ScaleAsymmetricDoesNotCrash)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  const std::shared_ptr<Sipi::SipiRegion> region;
  const auto size = std::make_shared<Sipi::SipiSize>("2,100");
  EXPECT_TRUE(img.read(leavesSmallNoAlpha, region, size).has_value());
}

// A 1-pixel target axis is degenerate for the bilinear interpolator (the
// original crash this suite guards against); the resamplers now reject it
// outright instead of silently leaving the image at its source size, so the
// non-crash and the honest rejection are both pinned here.
TEST(SipiImage, ScaleToOnePixelIsRejectedWithoutCrashing)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  const std::shared_ptr<Sipi::SipiRegion> region;
  const auto size = std::make_shared<Sipi::SipiSize>("1,1");
  const auto result = img.read(leavesSmallNoAlpha, region, size);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), Sipi::ErrorCode::kInvalidRequestParameter);
}

TEST(SipiImage, ScaleToOnePixelWidthOnlyIsRejectedWithoutCrashing)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  const std::shared_ptr<Sipi::SipiRegion> region;
  const auto size = std::make_shared<Sipi::SipiSize>("1,");
  const auto result = img.read(leavesSmallNoAlpha, region, size);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), Sipi::ErrorCode::kInvalidRequestParameter);
}

TEST(SipiImage, ScaleToOnePixelHeightOnlyIsRejectedWithoutCrashing)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  const std::shared_ptr<Sipi::SipiRegion> region;
  const auto size = std::make_shared<Sipi::SipiSize>(",1");
  const auto result = img.read(leavesSmallNoAlpha, region, size);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), Sipi::ErrorCode::kInvalidRequestParameter);
}

TEST(SipiImage, ScaleBoundary16bpsDoesNotCrash)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  const std::shared_ptr<Sipi::SipiRegion> region;
  const auto size = std::make_shared<Sipi::SipiSize>("2,2");
  EXPECT_TRUE(img.read(png16bit, region, size).has_value());
}

// `read` dispatches by extension, so a PNG file saved with a `.tif`
// extension sends the search through the TIFF handler first (mismatch),
// then the fallback loop across the remaining handlers, landing on PNG.
// The image must decode with the same dimensions as reading the real PNG.
TEST(SipiImage, ReadFallsBackWhenExtensionDoesNotMatchContent)
{
  const std::string misnamed = tmp_dir + "_mario_png_as_tif.tif";
  std::filesystem::copy_file(pngPaletteAlpha, misnamed, std::filesystem::copy_options::overwrite_existing);

  Sipi::SipiImage png_reference;
  ASSERT_TRUE(png_reference.read(pngPaletteAlpha).has_value());

  Sipi::SipiImage misnamed_image;
  ASSERT_TRUE(misnamed_image.read(misnamed).has_value());
  EXPECT_EQ(misnamed_image.getNx(), png_reference.getNx());
  EXPECT_EQ(misnamed_image.getNy(), png_reference.getNy());
}
