/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Regression test for the J2K decode-dimension guard (DEV-6063):
 * `validate_decode_dims()` caps each dimension a codec reads from a file's
 * header at `kMaxDecodeDim` before any decode buffer is sized from it.
 * `SipiIOJ2k::read()` calls it with the dimensions Kakadu reports from the
 * codestream `SIZ` marker segment, right after `codestream.get_dims()` and
 * before the palette/ICC handling or the stripe-decompressor buffer
 * allocation. This is an end-to-end check that the wiring — not just the
 * predicate, which the TIFF/PNG/JPEG handlers already share — rejects an
 * oversized J2K header via a crafted fixture; see
 * test/_test_data/images/malformed/README.md for how it was crafted.
 */

#include <gtest/gtest.h>

#include "image/SipiImage.h"
#include "image/SipiImageError.h"
#include "test_paths.h"

namespace {

using Sipi::SipiImage;
using Sipi::SipiImageError;

const std::string kOversizedDimensionsJp2 = sipi::test::data_dir() + "/images/malformed/j2k_oversized_dimensions.jp2";

// The fixture's SIZ marker segment declares Xsiz/Ysiz of 262144px
// (1 << 18), above kMaxDecodeDim (1 << 17); read() must reject it via
// validate_decode_dims() before any decode buffer is sized or allocated.
TEST(J2kDimensionRegression, RejectsOversizedDimensionsJp2EndToEnd)
{
  SipiImage img;
  EXPECT_THROW(img.read(kOversizedDimensionsJp2), SipiImageError);
}

}// namespace
