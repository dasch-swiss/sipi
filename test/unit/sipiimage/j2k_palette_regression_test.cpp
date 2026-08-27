/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Regression tests for J2K palette-color decode bugs (DEV-6065, N2):
 *
 * - The palette-expansion buffer in SipiIOJ2k::read() was sized with a
 *   hardcoded stride-3 assumption while the loop wrote at stride `numcol`
 *   read straight from the file's color-channel count; a palette image
 *   whose `numcol` differed from 3 overran the buffer.
 * - Each palette index read from the decoded pixel data was used as an
 *   offset into the R/G/B lookup tables with no bounds check against the
 *   palette's entry count, so a crafted index past the LUT size read out
 *   of bounds.
 * - `numcol` was used possibly-uninitialized when a file had no color
 *   layer at all.
 *
 * `Sipi::validate_j2k_palette_mapping()` is the fix: it rejects any
 * geometry the expansion loop can't handle (non-8-bit indices, more than
 * one index component, a color count other than 3, or a palette too small
 * to cover every possible index value) before the expansion buffer is ever
 * sized or written. Exercising it directly (rather than through a decoded
 * JP2) avoids depending on a Kakadu-encoded palette fixture, which SIPI's
 * own J2K writer does not produce and which requires license-gated Kakadu
 * tooling to construct externally.
 */

#include <gtest/gtest.h>

#include "formats/SipiIOJ2k.h"
#include "SipiImageError.h"

namespace {

using Sipi::SipiImageError;
using Sipi::validate_j2k_palette_mapping;

constexpr int kValidNumcol = 3;
constexpr int kValidNentries = 256;// covers every 8-bit index value

TEST(J2kPaletteRegression, AcceptsWellFormedEightBitRgbPalette)
{
  EXPECT_NO_THROW(validate_j2k_palette_mapping(8, 1, kValidNumcol, kValidNentries, "palette.jp2"));
}

TEST(J2kPaletteRegression, RejectsSixteenBitIndices)
{
  // bps == 16 indices are not handled by the expansion loop (it reads a
  // single index byte per pixel); must be rejected, not mis-decoded.
  EXPECT_THROW(validate_j2k_palette_mapping(16, 1, kValidNumcol, 65536, "palette.jp2"), SipiImageError);
}

TEST(J2kPaletteRegression, RejectsMultiComponentIndex)
{
  // More than one index component is not handled by the expansion loop's
  // single-component addressing.
  EXPECT_THROW(validate_j2k_palette_mapping(8, 2, kValidNumcol, kValidNentries, "palette.jp2"), SipiImageError);
}

TEST(J2kPaletteRegression, RejectsStrideMismatch)
{
  // numcol != 3 would leave the expansion buffer sized for `numcol`
  // channels while the loop writes at a hardcoded stride of 3 -- the N2
  // heap overflow. Must be rejected before the buffer is ever sized.
  EXPECT_THROW(validate_j2k_palette_mapping(8, 1, 4, kValidNentries, "palette.jp2"), SipiImageError);
  EXPECT_THROW(validate_j2k_palette_mapping(8, 1, 1, kValidNentries, "palette.jp2"), SipiImageError);
}

TEST(J2kPaletteRegression, RejectsUndersizedPalette)
{
  // An 8-bit index can be any value 0-255; a palette with fewer than 256
  // entries leaves some index values pointing past the LUT (DEV-6065).
  EXPECT_THROW(validate_j2k_palette_mapping(8, 1, kValidNumcol, 255, "palette.jp2"), SipiImageError);
  EXPECT_THROW(validate_j2k_palette_mapping(8, 1, kValidNumcol, 0, "palette.jp2"), SipiImageError);
}

}// namespace
