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

#include "../../../src/SipiImage.h"

namespace {

using Sipi::ExtraSamples;
using Sipi::PhotometricInterpretation;
using Sipi::SipiImage;

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
      img.setPixel(x, y, 0, 128);// Cr
      img.setPixel(x, y, 1, 128);// Cb
      img.setPixel(x, y, 2, 200);// Y
      img.setPixel(x, y, 3, static_cast<int>(1000 + idx));// pass-through channel, distinct per pixel
    }
  }

  ASSERT_NO_THROW(img.convertYCC2RGB());

  EXPECT_EQ(img.getNc(), nc);
  for (size_t y = 0; y < ny; ++y) {
    for (size_t x = 0; x < nx; ++x) {
      const size_t idx = y * nx + x;
      // Y=200, Cb=Cr=128 (mid-gray, zero chroma offset) -> R=G=B=200.
      EXPECT_EQ(img.getPixel(x, y, 0), 200) << "R at pixel " << idx;
      EXPECT_EQ(img.getPixel(x, y, 1), 200) << "G at pixel " << idx;
      EXPECT_EQ(img.getPixel(x, y, 2), 200) << "B at pixel " << idx;
      EXPECT_EQ(img.getPixel(x, y, 3), static_cast<int>(1000 + idx)) << "pass-through channel at pixel " << idx;
    }
  }
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

  ASSERT_NO_THROW(img.removeChannel(4, false));

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
