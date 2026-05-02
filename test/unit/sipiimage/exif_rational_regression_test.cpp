/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Regression test for DEV-6356 — uninitialized loop counter in
 * SipiIOTiff::readExif() corrupted EXIF rational arrays. The bug lived in
 * the `case EXIF_DT_RATIONAL_PTR` branch and was provably unreachable in
 * earlier SIPI revisions because no entry in `exiftag_list[]` carried that
 * datatype. This change adds `EXIFTAG_LENSSPECIFICATION` (RATIONAL[4]) to
 * the list, activating the path; the test asserts the array round-trips
 * correctly. Pre-fix, the inner counter was uninitialized and the values
 * returned were either wrong, missing, or caused a crash on OOB index.
 */

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "../../../src/SipiImage.hpp"
#include "SipiIOTiff.h"
#include "metadata/SipiExif.h"
#include "test_paths.hpp"

namespace {

static const std::string kFixturePath =
  sipi::test::data_dir() + "/images/unit/exif_lens_specification.tif";

// Values written by test/unit/sipiimage/fixtures/generate_exif_rational_tiff.cpp.
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
  ASSERT_NO_THROW(img.read(kFixturePath));
  auto exif = img.getExif();
  ASSERT_NE(exif, nullptr);

  std::vector<Exiv2::Rational> lens_spec;
  ASSERT_TRUE(exif->getValByKey("Exif.Photo.LensSpecification", lens_spec))
    << "LensSpecification not found in EXIF — fixture or read path broken";

  ASSERT_EQ(lens_spec.size(), 4u) << "LensSpecification must be RATIONAL[4]";

  // Each rational converts back to its float via numerator/denominator.
  // The fixture wrote 32-bit floats; libtiff stored them as RATIONAL pairs;
  // SipiExif::toRational converts back. Tolerance accounts for two passes
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

// LensMake / LensModel string round-trip is intentionally NOT asserted here.
// Both tags are present in the fixture (verifiable via exiftool) but
// SipiExif's std::string conversion is platform-dependent: on macOS the
// values come through cleanly, on Linux they collapse to the modified-UTF-8
// NUL sentinel `\xC0\x80` because Exiv2 reports the values as `unsignedByte`
// rather than `asciiString`. That is a separate SipiExif issue tracked
// independently from this rational-array fix.
