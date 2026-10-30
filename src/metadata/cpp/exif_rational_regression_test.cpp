/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Regression test — uninitialized loop counter in
 * SipiIOTiff::readExif() corrupted EXIF rational arrays. The bug lived in
 * the `case EXIF_DT_RATIONAL_PTR` branch and was provably unreachable in
 * earlier SIPI revisions because no entry in `exiftag_list[]` carried that
 * datatype. This change adds `EXIFTAG_LENSSPECIFICATION` (RATIONAL[4]) to
 * the list, activating the path; the test asserts the array round-trips
 * correctly. Pre-fix, the inner counter was uninitialized and the values
 * returned were either wrong, missing, or caused a crash on OOB index.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "error/SipiError.h"
#include "image/SipiImage.h"
#include "format_handlers/SipiIOTiff.h"
#include "metadata/exif.h"
#include "test_paths.h"

namespace {

static const std::string kFixturePath =
  sipi::test::data_dir() + "/images/unit/exif_lens_specification.tif";

// Values written by test/unit/fixtures/generate_exif_rational_tiff.cpp.
// LensSpecification per EXIF 2.31 §4.6.5: RATIONAL[4] =
//   [min focal length, max focal length, min F at min FL, min F at max FL]
constexpr float kExpectedLensSpec[4] = {70.0f, 200.0f, 2.8f, 2.8f};

}// namespace

/*! L1 — LensSpecification (RATIONAL[4]) round-trips through the
 *  EXIF_DT_RATIONAL_PTR case branch. On `main` (pre-fix), the inner loop
 *  counter was uninitialized; this test exposes that UB by asserting on the
 *  exact element values written by the fixture generator. */
TEST(ExifRationalRegression, LensSpecificationArrayRoundTrips)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  ASSERT_TRUE(img.read(kFixturePath).has_value());
  auto exif = img.getExif();
  ASSERT_NE(exif, nullptr);

  std::vector<Exiv2::Rational> lens_spec;
  ASSERT_TRUE(exif->getValByKey("Exif.Photo.LensSpecification", lens_spec))
    << "LensSpecification not found in EXIF — fixture or read path broken";

  ASSERT_EQ(lens_spec.size(), 4u) << "LensSpecification must be RATIONAL[4]";

  // Each rational converts back to its float via numerator/denominator.
  // The fixture wrote 32-bit floats; libtiff stored them as RATIONAL pairs;
  // Exif::toRational converts back. Tolerance accounts for two passes
  // of float<->rational conversion.
  for (size_t i = 0; i < 4; ++i) {
    const double recovered =
      static_cast<double>(lens_spec[i].first) / static_cast<double>(lens_spec[i].second);
    EXPECT_NEAR(recovered, kExpectedLensSpec[i], 0.01)
      << "LensSpecification[" << i << "] mismatch: "
      << lens_spec[i].first << "/" << lens_spec[i].second
      << " (= " << recovered << ", expected " << kExpectedLensSpec[i] << ")";
  }
}

/*! LensMake / LensModel (ASCII) round-trip through the EXIF_DT_STRING case
 *  branch. Pre-fix, `Exif::addKeyVal<std::string>` read the tag value through
 *  `(unsigned char *)&val, sizeof(T)`, i.e. the 24 bytes of the std::string
 *  object itself rather than its characters — every ASCII tag read from a
 *  TIFF EXIF IFD (DateTimeOriginal, LensMake, …) came out as 24 bytes of
 *  garbage, and ASan flagged the read past the short-string buffer as a
 *  container-overflow (nightly `tiff` fuzz leg). */
TEST(ExifRationalRegression, LensStringTagsRoundTrip)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  ASSERT_TRUE(img.read(kFixturePath).has_value());
  auto exif = img.getExif();
  ASSERT_NE(exif, nullptr);

  std::string lens_make;
  ASSERT_TRUE(exif->getValByKey("Exif.Photo.LensMake", lens_make));
  EXPECT_EQ(lens_make, "Nikon");

  std::string lens_model;
  ASSERT_TRUE(exif->getValByKey("Exif.Photo.LensModel", lens_model));
  EXPECT_EQ(lens_model, "AF-S Nikkor 70-200mm f/2.8E FL ED VR");
}

namespace {

const uint16_t kBrightnessValueTag = Exiv2::ExifKey("Exif.Photo.BrightnessValue").tag();
const uint16_t kLensMakeTag = Exiv2::ExifKey("Exif.Photo.LensMake").tag();
const uint16_t kLensSpecificationTag = Exiv2::ExifKey("Exif.Photo.LensSpecification").tag();

// Exif has no entry accessor, so count the lines of its operator<< text that start with `key `.
size_t key_occurrences(Sipi::Exif &exif, const std::string &key)
{
  std::ostringstream text;
  text << exif;
  std::istringstream lines(text.str());
  size_t count = 0;
  for (std::string line; std::getline(lines, line);) {
    if (line.starts_with(key + " ")) { ++count; }
  }
  return count;
}

}// namespace

TEST(ExifAddKeyVal, ScalarOverloadReplacesExistingValue)
{
  Sipi::Exif exif;
  exif.addKeyVal(kBrightnessValueTag, "Photo", Exiv2::Rational{ 1, 2 });
  exif.addKeyVal(kBrightnessValueTag, "Photo", Exiv2::Rational{ 3, 4 });

  Exiv2::Rational value;
  ASSERT_TRUE(exif.getValByKey(kBrightnessValueTag, "Photo", value));
  EXPECT_EQ(value, (Exiv2::Rational{ 3, 4 }));
  EXPECT_EQ(key_occurrences(exif, "Exif.Photo.BrightnessValue"), 1u);
}

TEST(ExifAddKeyVal, StringOverloadReplacesExistingValue)
{
  Sipi::Exif exif;
  exif.addKeyVal(kLensMakeTag, "Photo", std::string("Nikon"));
  exif.addKeyVal(kLensMakeTag, "Photo", std::string("Canon"));

  std::string value;
  ASSERT_TRUE(exif.getValByKey(kLensMakeTag, "Photo", value));
  EXPECT_EQ(value, "Canon");
  EXPECT_EQ(key_occurrences(exif, "Exif.Photo.LensMake"), 1u);
}

TEST(ExifAddKeyVal, PointerOverloadReplacesExistingValue)
{
  Sipi::Exif exif;
  const Exiv2::Rational first[4] = { { 70, 1 }, { 200, 1 }, { 28, 10 }, { 28, 10 } };
  const Exiv2::Rational second[4] = { { 24, 1 }, { 70, 1 }, { 4, 1 }, { 4, 1 } };
  exif.addKeyVal(kLensSpecificationTag, "Photo", first, 4);
  exif.addKeyVal(kLensSpecificationTag, "Photo", second, 4);

  std::vector<Exiv2::Rational> value;
  ASSERT_TRUE(exif.getValByKey(kLensSpecificationTag, "Photo", value));
  EXPECT_EQ(value, std::vector<Exiv2::Rational>(std::begin(second), std::end(second)));
  EXPECT_EQ(key_occurrences(exif, "Exif.Photo.LensSpecification"), 1u);
}

// image_orientation.jpg carries Exif.Photo.BrightnessValue as 0/0.
TEST(ExifAddKeyVal, ReplacesValueReadFromJpeg)
{
  Sipi::SipiImage img;
  ASSERT_TRUE(img.read(sipi::test::data_dir() + "/images/unit/image_orientation.jpg").has_value());
  auto exif = img.getExif();
  ASSERT_NE(exif, nullptr);
  ASSERT_EQ(key_occurrences(*exif, "Exif.Photo.BrightnessValue"), 1u);

  exif->addKeyVal(kBrightnessValueTag, "Photo", Exiv2::Rational{ 7, 2 });

  Exiv2::Rational value;
  ASSERT_TRUE(exif->getValByKey(kBrightnessValueTag, "Photo", value));
  EXPECT_EQ(value, (Exiv2::Rational{ 7, 2 }));
  EXPECT_EQ(key_occurrences(*exif, "Exif.Photo.BrightnessValue"), 1u);
}

TEST(ExifAddKeyVal, UnsupportedTypeThrowsAndKeepsExistingValue)
{
  Sipi::Exif exif;
  exif.addKeyVal(kBrightnessValueTag, "Photo", Exiv2::Rational{ 3, 4 });

  EXPECT_THROW(exif.addKeyVal(kBrightnessValueTag, "Photo", int64_t{ 5 }), Sipi::SipiError);
  const int64_t values[2] = { 5, 6 };
  EXPECT_THROW(exif.addKeyVal(kBrightnessValueTag, "Photo", values, 2), Sipi::SipiError);

  Exiv2::Rational value;
  ASSERT_TRUE(exif.getValByKey(kBrightnessValueTag, "Photo", value));
  EXPECT_EQ(value, (Exiv2::Rational{ 3, 4 }));
  EXPECT_EQ(key_occurrences(exif, "Exif.Photo.BrightnessValue"), 1u);
}
