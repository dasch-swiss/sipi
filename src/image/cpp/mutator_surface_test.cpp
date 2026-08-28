/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Covers the public pixel-buffer and metadata mutator surface added ahead of
 * the ADR-0007 `image_processing` extraction: `set_pixels`, `pixels_writable`/
 * `pixels_view`, and `exif_writable`'s lazy-initialisation.
 */

#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "SipiImage.h"
#include "SipiImageError.h"

namespace {

using Sipi::byte;
using Sipi::PhotometricInterpretation;
using Sipi::SipiImage;
using Sipi::SipiImageError;

TEST(MutatorSurface, SetPixelsAcceptsCorrectlySizedBufferAndUpdatesGeometry)
{
  SipiImage img(2, 2, 1, 8, PhotometricInterpretation::MINISBLACK);

  std::vector<byte> buf(3 * 4 * 1, byte{ 42 });
  img.set_pixels(std::move(buf), 3, 4, 1, 8);

  EXPECT_EQ(img.getNx(), 3u);
  EXPECT_EQ(img.getNy(), 4u);
  EXPECT_EQ(img.getNc(), 1u);
  EXPECT_EQ(img.getBps(), 8u);
  ASSERT_EQ(img.pixels_view().size(), 3u * 4u * 1u);
  EXPECT_EQ(img.pixels_view()[0], byte{ 42 });
}

TEST(MutatorSurface, SetPixelsRejectsSizeGeometryMismatch)
{
  SipiImage img(2, 2, 1, 8, PhotometricInterpretation::MINISBLACK);

  std::vector<byte> too_small(3 * 4 * 1 - 1, byte{ 0 });
  EXPECT_THROW(img.set_pixels(std::move(too_small), 3, 4, 1, 8), SipiImageError);
}

TEST(MutatorSurface, PixelsWritableAndViewReportCurrentSizeAndRoundTripAByte)
{
  SipiImage img(3, 2, 1, 8, PhotometricInterpretation::MINISBLACK);

  ASSERT_EQ(img.pixels_writable().size(), 3u * 2u * 1u);
  ASSERT_EQ(img.pixels_view().size(), 3u * 2u * 1u);

  img.pixels_writable()[4] = byte{ 200 };
  EXPECT_EQ(img.pixels_view()[4], byte{ 200 });
}

TEST(MutatorSurface, ExifWritableLazilyAllocatesWhenExifIsNull)
{
  SipiImage source;
  // Moving out of `source` leaves its `exif` shared_ptr null (shared_ptr's
  // move constructor guarantees an empty moved-from source) — the only
  // way to reach a null-Exif SipiImage through the public API, since every
  // constructor otherwise ensures one.
  SipiImage moved(std::move(source));
  ASSERT_EQ(source.getExif(), nullptr);

  auto e = source.exif_writable();
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(source.getExif(), e);
}

}// namespace
