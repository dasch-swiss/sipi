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
 * sized or written.
 *
 * Coverage is two-layered:
 *   1. Direct calls to validate_j2k_palette_mapping() below pin every
 *      rejection branch precisely and cheaply.
 *   2. Two on-disk fixtures drive the whole SipiIOJ2k::read() decode path:
 *      - a well-formed palette JP2 (file9.jp2 from the ISO/IEC 15444-4
 *        conformance suite: NC=1 8-bit index, pclr with 256 entries and
 *        3 RGB columns, cmap mapping the single component to those 3
 *        colours) must decode and expand to a 3-channel RGB image;
 *      - a malformed variant of it (pclr NE shrunk from 256 to 128, so an
 *        8-bit index can point past the LUT) must be rejected by the guard
 *        that read() calls with the file-derived geometry — proving the
 *        wiring, not just the predicate. SIPI's own J2K writer produces no
 *        palette JP2 (it decodes palette->RGB and has no palette-encode
 *        path), so the malformed file is hand-edited from file9.jp2; see
 *        test/_test_data/images/malformed/README.md.
 */

#include <gtest/gtest.h>

#include "SipiImage.h"
#include "SipiImageError.h"
#include "formats/SipiIOJ2k.h"
#include "test_paths.h"

namespace {

using Sipi::PhotometricInterpretation;
using Sipi::SipiImage;
using Sipi::SipiImageError;
using Sipi::validate_j2k_palette_mapping;

const std::string kPaletteJp2 = sipi::test::data_dir() + "/images/iso-15444-4/testfiles_jp2/file9.jp2";
const std::string kUndersizedPaletteJp2 = sipi::test::data_dir() + "/images/malformed/palette_undersized_lut.jp2";

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

// End-to-end: a well-formed palette JP2 decodes and the single 8-bit index
// component is expanded through the R/G/B LUTs into a 3-channel RGB image at
// the source geometry. Post-fix, the stride-3 expansion buffer matches the
// numcol == 3 the file declares, so no write runs past its end.
TEST(J2kPaletteRegression, DecodesWellFormedPaletteJp2EndToEnd)
{
  SipiImage img;
  ASSERT_NO_THROW(img.read(kPaletteJp2));

  EXPECT_EQ(img.getNx(), 768u);
  EXPECT_EQ(img.getNy(), 512u);
  EXPECT_EQ(img.getNc(), 3u);// NC=1 index expanded to R,G,B via the palette
  EXPECT_EQ(img.getBps(), 8u);
  EXPECT_EQ(img.getPhoto(), PhotometricInterpretation::RGB);
}

// End-to-end: the malformed variant carries an 8-bit index but only 128
// palette entries, so index values 128..255 would read past the LUT. read()
// must reject it via validate_j2k_palette_mapping() before sizing or writing
// the expansion buffer — Kakadu parses the box structure fine, so the guard
// (not Kakadu) is what fails it closed.
TEST(J2kPaletteRegression, RejectsUndersizedPaletteJp2EndToEnd)
{
  SipiImage img;
  EXPECT_THROW(img.read(kUndersizedPaletteJp2), SipiImageError);
}

}// namespace
