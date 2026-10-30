/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * JPEG 2000 YCbCr decode honours the JP2 channel mapping. file2.jp2 (ISO/IEC
 * 15444-4) carries a cdef box that maps codestream components 0/1/2 to
 * Cr/Cb/Y; unit/ycbcr16.jpx has no cdef box, so its components are Y/Cb/Cr.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "image/SipiImage.h"
#include "image_processing/processing.h"
#include "test_paths.h"

namespace {

using Sipi::PhotometricInterpretation;
using Sipi::SipiImage;

const std::string kConformance = sipi::test::data_dir() + "/images/iso-15444-4";
const std::string kYcbcr16 = sipi::test::data_dir() + "/images/unit/ycbcr16.jpx";

TEST(J2kChannelOrder, ReversedCdefSyccMatchesReference)
{
  SipiImage decoded;
  ASSERT_TRUE(decoded.read(kConformance + "/testfiles_jp2/file2.jp2").has_value());
  SipiImage reference;
  ASSERT_TRUE(reference.read(kConformance + "/reference_jp2/jp2_2.tif").has_value());
  ASSERT_EQ(decoded.getNx(), reference.getNx());
  ASSERT_EQ(decoded.getNy(), reference.getNy());
  ASSERT_EQ(decoded.getNc(), reference.getNc());

  const auto delta = Sipi::processing::maxPixelDelta(decoded, reference);
  ASSERT_TRUE(delta.has_value());
  EXPECT_LE(delta->mean_abs, 1.0);
}

// ycbcr16.jpx has no colour ground truth (RGB samples behind a colr box
// flipped to sYCC), so the check is against its own raw components: a copy
// with the colr box set back to sRGB decodes them unconverted, and the YCbCr
// decode must equal the full-range conversion with Y, Cb, Cr taken from
// components 0, 1, 2.
TEST(J2kChannelOrder, NoCdefYccConvertsComponentsInCodestreamOrder)
{
  std::ifstream in(kYcbcr16, std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const std::string colr = "colr";
  const auto box = std::search(bytes.begin(), bytes.end(), colr.begin(), colr.end());
  ASSERT_NE(box, bytes.end());
  const std::size_t enum_cs = static_cast<std::size_t>(box - bytes.begin()) + 7;// METH, PREC, APPROX, then EnumCS
  ASSERT_EQ(bytes.at(enum_cs + 3), 18);// sYCC
  bytes.at(enum_cs + 3) = 16;// sRGB
  const std::string raw_path = sipi::test::tmp_dir() + "/_ycbcr16_as_srgb.jpx";
  {
    std::ofstream out(raw_path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }

  SipiImage raw;
  ASSERT_TRUE(raw.read(raw_path).has_value());
  SipiImage decoded;
  ASSERT_TRUE(decoded.read(kYcbcr16).has_value());
  ASSERT_EQ(decoded.getPhoto(), PhotometricInterpretation::RGB);
  ASSERT_EQ(raw.getNx(), decoded.getNx());
  ASSERT_EQ(raw.getNy(), decoded.getNy());
  ASSERT_EQ(raw.getNc(), 3u);
  ASSERT_EQ(decoded.getNc(), 3u);
  ASSERT_EQ(decoded.getBps(), 16u);

  // The check only tells the orders apart where components 0 and 2 differ.
  bool c0_differs_from_c2 = false;
  for (std::size_t y = 0; y < raw.getNy(); ++y) {
    for (std::size_t x = 0; x < raw.getNx(); ++x) {
      const double Y = raw.getPixel(x, y, 0);
      const double Cb = raw.getPixel(x, y, 1);
      const double Cr = raw.getPixel(x, y, 2);
      c0_differs_from_c2 = c0_differs_from_c2 || Y != Cr;
      const int r = std::clamp(static_cast<int>(Y + 1.40200 * (Cr - 32768)), 0, 65535);
      const int g = std::clamp(static_cast<int>(Y - 0.34414 * (Cb - 32768) - 0.71414 * (Cr - 32768)), 0, 65535);
      const int b = std::clamp(static_cast<int>(Y + 1.77200 * (Cb - 32768)), 0, 65535);
      EXPECT_EQ(decoded.getPixel(x, y, 0), r) << "R at " << x << "," << y;
      EXPECT_EQ(decoded.getPixel(x, y, 1), g) << "G at " << x << "," << y;
      EXPECT_EQ(decoded.getPixel(x, y, 2), b) << "B at " << x << "," << y;
    }
  }
  EXPECT_TRUE(c0_differs_from_c2);
}

}// namespace
