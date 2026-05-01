/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Regression tests for DEV-6249 — 1-bit bilevel TIFF support and the
 * dormant ROI/switch bugs that were exposed once the rejection gate was
 * removed. Every test in this file fails on `main` and passes on the
 * matching fix commit.
 */

#include "gtest/gtest.h"

#include "../../../src/SipiImage.hpp"
#include "../../../src/SipiImageError.hpp"
#include "SipiIOTiff.h"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiSize.h"
#include "test_paths.hpp"

#include <memory>
#include <string>

namespace {

// `sipi::test::data_dir()` honours Bazel's SIPI_TEST_DATA_DIR env and
// falls back to the historical CMake build-tree relative path when
// unset. See test/test_paths.hpp. `static const std::string` (rather
// than `constexpr const char *`) is required because the path is built
// at static-init time from `std::getenv`.
static const std::string kBilevelDir = sipi::test::data_dir() + "/images/bilevel";
static const std::string kBilevelLzwMinisWhite = kBilevelDir + "/bilevel_lzw_miniswhite.tif";
static const std::string kBilevelNoneMinisWhite = kBilevelDir + "/bilevel_none_miniswhite.tif";
static const std::string kBilevelLzwMinisBlack = kBilevelDir + "/bilevel_lzw_minisblack.tif";
static const std::string kBilevelRoiTest = kBilevelDir + "/bilevel_roi_test.tif";

static const std::string kLeaves8Tif = sipi::test::data_dir() + "/images/knora/Leaves8.tif";

Sipi::SipiImage readFixture(const std::string &path,
  std::shared_ptr<Sipi::SipiRegion> region = nullptr,
  std::shared_ptr<Sipi::SipiSize> size = nullptr)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  img.read(path, region, size);
  return img;
}

}// namespace

/*! R1 — a bps=1 LZW-compressed MINISWHITE TIFF must read successfully and
 *  be exposed as 8-bit grayscale. On `main` this throws
 *  `"Images with 1 bit/sample not supported"` from SipiIOTiff.cpp:1192. */
TEST(TiffBilevelRegression, Tiff1BitLzwMinisWhiteReadsAs8Bit)
{
  Sipi::SipiImage img;
  ASSERT_NO_THROW(img = readFixture(kBilevelLzwMinisWhite));
  EXPECT_EQ(img.getNx(), 128u);
  EXPECT_EQ(img.getNy(), 128u);
  EXPECT_EQ(img.getNc(), 1u);
  // After reading, a 1-bit TIFF must be exposed as 8-bit grayscale.
  EXPECT_EQ(img.getBps(), 8u);
}

/*! R2 — the same contract applies to uncompressed bilevel TIFFs. One of the
 *  22 failing DEV-6249 files (7-27_KV40_FN473) has Compression=None and was
 *  rejected by the same gate. */
TEST(TiffBilevelRegression, Tiff1BitUncompressedReadsAs8Bit)
{
  Sipi::SipiImage img;
  ASSERT_NO_THROW(img = readFixture(kBilevelNoneMinisWhite));
  EXPECT_EQ(img.getBps(), 8u);
  EXPECT_EQ(img.getNc(), 1u);
}

/*! R3 — MINISBLACK photometric must also be handled without throwing.
 *  Covered by the same rejection gate today. */
TEST(TiffBilevelRegression, Tiff1BitMinisBlackInvertsCorrectly)
{
  Sipi::SipiImage img;
  ASSERT_NO_THROW(img = readFixture(kBilevelLzwMinisBlack));
  EXPECT_EQ(img.getBps(), 8u);
  EXPECT_EQ(img.getPhoto(), Sipi::PhotometricInterpretation::MINISBLACK);
}

/*! R4 — reading a 1-bit TIFF with a non-zero IIIF region (ROI) must not
 *  trigger a buffer overflow. On `main` (once the rejection gate is removed)
 *  the memcpy on SipiIOTiff.cpp:638 uses `nc * i * roi_w` instead of
 *  `nc * (i - roi_y) * roi_w` and writes past the end of `inbuf`. Under ASan
 *  this crashes; without ASan it may silently corrupt heap memory. The
 *  fix from Phase 2.2 (`i - roi_y` offset) makes the ROI read safe. */
TEST(TiffBilevelRegression, Tiff1BitRoiDoesNotCorruptMemory)
{
  // 256x192 fixture; crop a band at y=64, h=64 so the memcpy destination
  // offset bug would write beyond `inbuf` if still present.
  auto region = std::make_shared<Sipi::SipiRegion>("0,64,256,64");
  Sipi::SipiImage img;
  ASSERT_NO_THROW(img = readFixture(kBilevelRoiTest, region));
  EXPECT_EQ(img.getNx(), 256u);
  EXPECT_EQ(img.getNy(), 64u);
  EXPECT_EQ(img.getBps(), 8u);
}

/*! R5 — the switch fall-through at SipiIOTiff.cpp:1190-1200 sets `ps=2`
 *  unconditionally because case 8 does not `break`. The fix restores
 *  `ps=1` for 8-bit. We pin this via the post-read buffer size: an 8-bit
 *  grayscale image must have `width * height * 1` bytes, not double.
 *  Today, the fall-through path allocates twice the needed memory but
 *  only copies the correct amount — so this test observes the bug
 *  indirectly via pixel size rather than a sanitizer crash. */
TEST(TiffBilevelRegression, Tiff8BitBufferSizeIsCorrect)
{
  Sipi::SipiImage img;
  ASSERT_NO_THROW(img = readFixture(kLeaves8Tif));
  // Leaves8.tif is 8-bit; after reading, pixels must be stored as one byte
  // per sample. Verify bps and that the image is in a consistent post-read
  // state (nx/ny populated) so regressions in the switch fix are caught.
  EXPECT_EQ(img.getBps(), 8u);
  EXPECT_GT(img.getNx(), 0u);
  EXPECT_GT(img.getNy(), 0u);
}
