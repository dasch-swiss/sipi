/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Regression test for the J2K embedded-metadata fatal-parse contract
 * (DEV-7056): `SipiIOJ2k::read()` walks a JP2/JPX file's top-level boxes and,
 * on finding a `uuid` box carrying the EXIF UUID (`JpgTiffExif->JP2`), hands
 * the remaining box bytes to `Exif::parse()`. A failure there is fatal — SIPI
 * is a repository and must not admit a file with corrupt embedded metadata —
 * and is reported as `ErrorCode::kMetadataParseFailed`, mirroring the JPEG
 * and TIFF handlers' coverage of the same contract; see
 * test/_test_data/images/malformed/README.md for how the fixture was
 * crafted.
 */

#include <gtest/gtest.h>

#include "error/SipiValueError.h"
#include "image/SipiImage.h"
#include "test_paths.h"

namespace {

using Sipi::ErrorCode;
using Sipi::SipiImage;

const std::string kExifTruncatedJp2 = sipi::test::data_dir() + "/images/malformed/j2k_exif_truncated.jp2";

// The appended uuid box's payload is `Exif\0\0`-free — it is the bare
// little-endian TIFF prefix `"II*"` (byte-order mark + magic-number high
// byte only, no low byte, no IFD offset, no IFD) that `Exif::parse()` (via
// `Exiv2::ExifParser::decode()`) genuinely rejects. read() reports the
// failure at the metadata stage, before returning any decoded image.
TEST(J2kMetadataRegression, RejectsExifTruncatedJp2EndToEnd)
{
  SipiImage img;
  auto result = img.read(kExifTruncatedJp2);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::kMetadataParseFailed);
}

}// namespace
