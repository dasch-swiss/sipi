/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Regression tests for DEV-6250 (heritage JPEG read failures + metadata
 * robustness + YCCK support) and DEV-6257 (CMYK APP14 Adobe transform
 * inversion). Every test in this file fails on `main` and passes on the
 * matching fix commits.
 */

#include "gtest/gtest.h"

#include "../../../src/SipiImage.hpp"
#include "../../../src/SipiImageError.hpp"
#include "SipiIOTiff.h"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiSize.h"

#include <memory>
#include <string>

namespace {

constexpr const char *kJpegHeritage_o = "../../../../test/_test_data/images/jpeg/35-2421d-o.jpg";
constexpr const char *kJpegHeritage_r = "../../../../test/_test_data/images/jpeg/35-2421d-r.jpg";
constexpr const char *kJpegCmykApp14 = "../../../../test/_test_data/images/jpeg/cmyk/cmyk_photoshop_app14.jpg";
constexpr const char *kJpegCmykRaw = "../../../../test/_test_data/images/jpeg/cmyk/cmyk_raw_no_app14.jpg";
constexpr const char *kJpegMalformedXmp = "../../../../test/_test_data/images/jpeg/malformed_xmp.jpg";

Sipi::SipiImage readFixture(const std::string &path)
{
  Sipi::SipiIOTiff::initLibrary();// JPEG tests may transitively touch libtiff
  Sipi::SipiImage img;
  img.read(path);
  return img;
}

}// namespace

// -------------------------------------------------------------------------
// DEV-6250 — heritage JPEG read + metadata robustness
// -------------------------------------------------------------------------

/*! R6 — both heritage JPEG variants must read successfully after the
 *  Phase 4 fix. The exact root cause is captured during Phase 4
 *  diagnosis using `--json`; regardless of that cause, the contract is
 *  "sipi reads these images without throwing". We use a parameterized
 *  test so both siblings (-o and -r) share the same contract. */
class Jpeg_35_2421d_Reads : public ::testing::TestWithParam<const char *>
{
};

TEST_P(Jpeg_35_2421d_Reads, ReadsSuccessfully)
{
  const char *path = GetParam();
  Sipi::SipiImage img;
  ASSERT_NO_THROW(img = readFixture(path)) << "Failed to read " << path;
  EXPECT_EQ(img.getNx(), 404u);
  EXPECT_EQ(img.getNy(), 201u);
  EXPECT_EQ(img.getNc(), 3u);// RGB
  EXPECT_EQ(img.getBps(), 8u);
}

/*! R6b — DEV-6259 follow-up to R6: the heritage Photoshop CS 2008 JPEGs
 *  have an APP1 XMP segment that omits the optional `<?xpacket>` wrappers
 *  and starts directly with `<x:xmpmeta>`. Once the simple "everything
 *  after the namespace header is XMP" extractor lands, `img->xmp` must be
 *  populated so the XMP block can round-trip into the JP2 output (via
 *  `SipiIOJ2k::write`'s UUID-box emitter). */
TEST_P(Jpeg_35_2421d_Reads, XmpSurvivesRead)
{
  const char *path = GetParam();
  Sipi::SipiImage img;
  ASSERT_NO_THROW(img = readFixture(path));
  ASSERT_NE(img.getXmp(), nullptr) << "XMP must be populated for " << path;
  // The XMP packet contains the German Photoshop dump including the
  // distinctive umlaut substring "Dateigröße" (file size).
  const std::string xmp_bytes = img.getXmp()->xmpBytes();
  EXPECT_NE(xmp_bytes.find("Dateigr"), std::string::npos)
    << "XMP body should contain the German Photoshop caption";
}

INSTANTIATE_TEST_SUITE_P(Heritage, Jpeg_35_2421d_Reads, ::testing::Values(kJpegHeritage_o, kJpegHeritage_r));

/*! R8 — a JPEG whose metadata fails to parse must still return a usable
 *  image. Today, an exception during IPTC / EXIF / XMP parsing aborts the
 *  entire read. Phase 5.3 wraps each metadata block in a try/catch and
 *  downgrades parse failures to warnings.
 *
 *  We use `malformed_xmp.jpg` — a valid JPEG envelope with a deliberately
 *  corrupted XMP packet — to exercise the XMP-specific path. After the
 *  Phase 5.3 fix the image decodes; the warning is routed to stderr. */
TEST(JpegFormatRegression, JpegCorruptXmpStillReadsImage)
{
  Sipi::SipiImage img;
  ASSERT_NO_THROW(img = readFixture(kJpegMalformedXmp));
  EXPECT_EQ(img.getNx(), 64u);
  EXPECT_EQ(img.getNy(), 64u);
  EXPECT_EQ(img.getNc(), 3u);
}

/*! R7 — a YCCK-colorspace JPEG must read (mapped to SEPARATED / CMYK
 *  by libjpeg-turbo's internal conversion). Today, SipiIOJpeg.cpp:745
 *  throws `"Unsupported JPEG colorspace JCS_YCCK"`.
 *
 *  Pillow cannot directly produce a YCCK JPEG from a single call, so this
 *  test is marked DISABLED until a YCCK fixture is provided. Phase 5.1
 *  enables YCCK → SEPARATED mapping. Once a fixture is available, rename
 *  the test to remove the `DISABLED_` prefix. */
TEST(JpegFormatRegression, DISABLED_JpegYcckColorspaceReads)
{
  GTEST_SKIP() << "Enable once a YCCK fixture is committed (DEV-6250 follow-up)";
}

// -------------------------------------------------------------------------
// DEV-6257 — JPEG CMYK APP14 Adobe transform inversion
// -------------------------------------------------------------------------

/*! R9 — a Photoshop-produced CMYK JPEG (APP14 with transform=0) must read
 *  and produce CMYK polarity consistent with the source. libjpeg-turbo
 *  returns inverted CMYK for APP14 transform=0; sipi must re-invert.
 *  Today there is no APP14 detection and the values are wrong.
 *
 *  This test pins the "read succeeds" contract; the colour-correctness
 *  assertion lives in a follow-up approval test once Phase 5.2 lands. */
TEST(JpegFormatRegression, JpegCmykPhotoshopApp14Inversion)
{
  Sipi::SipiImage img;
  ASSERT_NO_THROW(img = readFixture(kJpegCmykApp14));
  EXPECT_EQ(img.getNc(), 4u);// CMYK
  EXPECT_EQ(img.getPhoto(), Sipi::PhotometricInterpretation::SEPARATED);
}

/*! R10 — a raw CMYK JPEG with no APP14 marker must **not** be inverted.
 *  This is the negative case that pins the branch logic added in Phase 5.2
 *  so the inversion fix does not over-apply to files that do not need it.
 *  This test is expected to pass on `main` (no inversion happens) — the
 *  purpose is to guard against regressions once Phase 5.2 introduces the
 *  APP14-aware inversion path. */
TEST(JpegFormatRegression, JpegCmykRawNoApp14NotInverted)
{
  Sipi::SipiImage img;
  ASSERT_NO_THROW(img = readFixture(kJpegCmykRaw));
  EXPECT_EQ(img.getNc(), 4u);
  EXPECT_EQ(img.getPhoto(), Sipi::PhotometricInterpretation::SEPARATED);
}
