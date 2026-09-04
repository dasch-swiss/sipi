/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Regression tests for two channel-count bugs in SipiImage.cpp (DEV-6068, N5):
 *
 * - convertYCC2RGB()'s 16-bit branch sized its output buffer with `nc - 1`
 *   channels while indexing every read/write with `nc` — a conversion that
 *   does not drop a channel (unlike removeChannel below), so the buffer was
 *   one channel too small and the last pixel's writes ran past its end.
 * - removeChannel()'s pixel-compaction lambdas skipped the removed channel's
 *   slot in the *input* index but wrote every surviving channel at its
 *   original (uncompacted) output index, so removing a non-terminal channel
 *   left a gap and overran the narrower output row for later channels.
 */

#include <gtest/gtest.h>

#include "image/SipiImage.h"
#include "image_processing/processing.h"
#include "test_paths.h"

namespace {

using Sipi::ErrorCode;
using Sipi::ExtraSamples;
using Sipi::PhotometricInterpretation;
using Sipi::SipiImage;

// End-to-end companion to ConvertYcc16BitDoesNotOverflow below: a real
// 16-bit, 3-component JP2 whose colour box declares the sYCC enumerated
// colourspace, so SipiIOJ2k::read() sets photo == YCBCR and bps == 16 and
// runs convertYCC2RGB()'s 16-bit branch in situ (rather than the crafted
// in-memory image the unit test drives). Provenance: an 8x8 16-bit RGB
// codestream written by `sipi convert`, whose enumerated colr box was then
// flipped from sRGB (EnumCS 16) to sYCC (EnumCS 18) so the reader takes the
// YCbCr path — the standards-valid way a JP2 signals YCbCr.
//
// Local build can't run ASan (broken libc++ link on macOS); a clean decode
// to the right geometry is the local signal, the CI asan-ubsan job is the
// gate that would catch the original `nc - 1`-sized-buffer overflow.
const std::string kYcbcr16Jp2 = sipi::test::data_dir() + "/images/unit/ycbcr16.jpx";

// removeChannel() only operates on registered extra-sample channels (`es`),
// which is a protected member with no public mutator — a thin subclass gets
// constructor-inherited access so the test can register extra samples
// directly, without a checked-in fixture or a real TIFF round-trip.
class TestableSipiImage : public SipiImage
{
public:
  using SipiImage::SipiImage;

  void addExtraSample(ExtraSamples sample) { es.push_back(sample); }
  [[nodiscard]] ExtraSamples extraSampleAt(size_t i) const { return es.at(i); }
};

// convertYCC2RGB() indexes channels 0..2 unconditionally; a single-channel
// image (e.g. a JP2 whose colr box claims sYCC but whose codestream carries
// Csiz=1) has no channel-count guard before that indexing runs. Pre-fix this
// is a 2-4 byte heap write past the single-channel pixel buffer, observable
// under the CI asan-ubsan leg (local ASan is broken, per repo convention);
// post-fix it is a clean kMalformedInput rejection.
TEST(ConvertYcc, RejectsTooFewChannels8Bit)
{
  SipiImage img(2, 2, 1, 8, PhotometricInterpretation::MINISBLACK);
  const auto result = Sipi::processing::convertYCC2RGB(img);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::kMalformedInput);
}

TEST(ConvertYcc, RejectsTooFewChannels16Bit)
{
  SipiImage img(2, 2, 1, 16, PhotometricInterpretation::MINISBLACK);
  const auto result = Sipi::processing::convertYCC2RGB(img);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::kMalformedInput);
}

// The 16-bit YCbCr->RGB conversion keeps all `nc` channels (channels beyond
// the first three are copied through unchanged); it must size and index its
// output buffer with the same `nc` the 8-bit branch uses. A 4-channel image
// with 2x2 pixels is enough that the old `nc - 1`-sized buffer overruns on
// the last pixel's pass-through channel.
TEST(ConvertYcc, ConvertYcc16BitDoesNotOverflow)
{
  constexpr size_t nx = 2, ny = 2, nc = 4;
  SipiImage img(nx, ny, nc, 16, PhotometricInterpretation::YCBCR);

  for (size_t y = 0; y < ny; ++y) {
    for (size_t x = 0; x < nx; ++x) {
      const size_t idx = y * nx + x;
      img.setPixel(x, y, 0, 32768);// Cr, 16-bit chroma midpoint
      img.setPixel(x, y, 1, 32768);// Cb, 16-bit chroma midpoint
      img.setPixel(x, y, 2, 200);// Y
      img.setPixel(x, y, 3, static_cast<int>(1000 + idx));// pass-through channel, distinct per pixel
    }
  }

  const auto result = Sipi::processing::convertYCC2RGB(img);
  ASSERT_TRUE(result.has_value());

  EXPECT_EQ(img.getNc(), nc);
  for (size_t y = 0; y < ny; ++y) {
    for (size_t x = 0; x < nx; ++x) {
      const size_t idx = y * nx + x;
      // Y=200, Cb=Cr=32768 (16-bit mid-gray, zero chroma offset) -> R=G=B=200.
      EXPECT_EQ(img.getPixel(x, y, 0), 200) << "R at pixel " << idx;
      EXPECT_EQ(img.getPixel(x, y, 1), 200) << "G at pixel " << idx;
      EXPECT_EQ(img.getPixel(x, y, 2), 200) << "B at pixel " << idx;
      EXPECT_EQ(img.getPixel(x, y, 3), static_cast<int>(1000 + idx)) << "pass-through channel at pixel " << idx;
    }
  }
}

// Reads the real 16-bit sYCC JP2 end-to-end through SipiIOJ2k::read(), which
// takes the bps == 16 decode branch and then convertYCC2RGB()'s 16-bit path.
// Post-fix, that path sizes and indexes its output buffer with the same `nc`,
// so the decode completes and yields a 3-channel 16-bit RGB image at the
// original geometry. Pre-fix, the `nc - 1`-sized buffer overran on the last
// pixel (detected by ASan on CI).
TEST(ConvertYcc, Decodes16BitYcbcrJp2EndToEnd)
{
  SipiImage img;
  ASSERT_TRUE(img.read(kYcbcr16Jp2).has_value());

  EXPECT_EQ(img.getNx(), 8u);
  EXPECT_EQ(img.getNy(), 8u);
  EXPECT_EQ(img.getNc(), 3u);// index component expanded to R,G,B; no channel dropped
  EXPECT_EQ(img.getBps(), 16u);// the 16-bit convertYCC2RGB branch, not the 8-bit one
  // read() flips YCbCr to RGB once the conversion has run.
  EXPECT_EQ(img.getPhoto(), PhotometricInterpretation::RGB);
}

// A SEPARATED (CMYK-family) image with 2 extra samples beyond the 4 content
// channels — RGB caps at a single extra sample (nc == 4 only), so SEPARATED
// with nc == 6 is the smallest photometric interpretation that lets the
// constructor build a >=2-extra-sample image without an on-disk fixture.
// Removing the first (non-terminal) extra sample must shift the second one
// down by one slot rather than leaving a gap that overruns the output row.
TEST(RemoveChannel, RemoveNonTerminalChannel)
{
  constexpr size_t nx = 2, ny = 2, nc = 6;
  TestableSipiImage img(nx, ny, nc, 8, PhotometricInterpretation::SEPARATED);
  img.addExtraSample(ExtraSamples::ASSOCALPHA);// channel 4 (removed below)
  img.addExtraSample(ExtraSamples::UNASSALPHA);// channel 5 (must survive, shifted to 4)

  for (size_t y = 0; y < ny; ++y) {
    for (size_t x = 0; x < nx; ++x) {
      const size_t idx = y * nx + x;
      for (size_t c = 0; c < nc; ++c) { img.setPixel(x, y, c, static_cast<int>(c * 10 + idx)); }
    }
  }

  const auto result = Sipi::processing::removeChannel(img, 4, false);
  ASSERT_TRUE(result.has_value());

  ASSERT_EQ(img.getNc(), 5u);
  ASSERT_EQ(img.getNalpha(), 1u);
  EXPECT_EQ(img.extraSampleAt(0), ExtraSamples::UNASSALPHA);

  for (size_t y = 0; y < ny; ++y) {
    for (size_t x = 0; x < nx; ++x) {
      const size_t idx = y * nx + x;
      for (size_t c = 0; c < 4; ++c) {
        EXPECT_EQ(img.getPixel(x, y, c), static_cast<int>(c * 10 + idx)) << "channel " << c << " at pixel " << idx;
      }
      // Old channel 5 survives, compacted down into slot 4.
      EXPECT_EQ(img.getPixel(x, y, 4), static_cast<int>(5 * 10 + idx)) << "compacted channel at pixel " << idx;
    }
  }
}

}// namespace
