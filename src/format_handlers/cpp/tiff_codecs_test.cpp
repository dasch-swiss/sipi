/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

// Codec-coverage proof for the native libtiff cc_library (DEV-6563).
//
// Stock BCR libtiff ships JPEG / LZMA / ZSTD / WebP-in-TIFF DISABLED; the
// vendored ext/tiff rule re-enables them (plus JBIG, a capability add) via the
// SUPPORT defines in tiffconf.h / tif_config.h. This test is the regression
// gate that those defines actually took effect — TIFFIsCODECConfigured() returns
// false for a codec whose SUPPORT macro landed in the wrong header or got lost
// on a version bump. The ZSTD round-trip proves a re-enabled codec encodes AND
// decodes end-to-end, not merely that the registry lists it.

#include "gtest/gtest.h"

#include "tiff.h"
#include "tiffio.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

std::string scratch_path(const char *name)
{
  const char *dir = std::getenv("TEST_TMPDIR");
  return std::string(dir ? dir : "/tmp") + "/" + name;
}

// RAII for the libtiff handle so a failing ASSERT_* (which returns from the
// function) still closes it.
using TiffPtr = std::unique_ptr<TIFF, void (*)(TIFF *)>;
TiffPtr tiff_open(const std::string &path, const char *mode)
{
  return TiffPtr(TIFFOpen(path.c_str(), mode), &TIFFClose);
}

}// namespace

// Every codec SIPI's archive ingest depends on must be compiled into libtiff.
// CCITTFAX4 / LZW / Deflate are the classic write codecs; JPEG / LZMA / ZSTD /
// WebP / JBIG are the ones stock BCR disables.
TEST(TiffCodecs, AllExpectedCodecsConfigured)
{
  struct Codec
  {
    int scheme;
    const char *name;
  };
  const Codec codecs[] = {
    {COMPRESSION_CCITTFAX4, "CCITTFAX4"},
    {COMPRESSION_LZW, "LZW"},
    {COMPRESSION_ADOBE_DEFLATE, "ADOBE_DEFLATE"},
    {COMPRESSION_JPEG, "JPEG"},
    {COMPRESSION_LZMA, "LZMA"},
    {COMPRESSION_ZSTD, "ZSTD"},
    {COMPRESSION_WEBP, "WEBP"},
    {COMPRESSION_JBIG, "JBIG"},
  };
  for (const auto &c : codecs) {
    EXPECT_TRUE(TIFFIsCODECConfigured(static_cast<uint16_t>(c.scheme)))
      << "libtiff codec not configured: " << c.name << " (scheme " << c.scheme << ")";
  }
}

// End-to-end proof that the re-enabled lossless compressors actually encode AND
// decode: write a 16×16 RGB raster with each scheme, read it back, assert
// byte-identical pixels. ZSTD + LZMA are the codecs stock BCR disables; LZW +
// Deflate are the classic ones. (JBIG/CCITTFAX4 need a bilevel raster and WebP
// defaults to lossy, so those rely on the configured-codec assertion above; the
// headline lossy JPEG-in-TIFF read path is exercised by the approval goldens.)
TEST(TiffCodecs, LosslessRgbRoundTrip)
{
  struct Codec
  {
    int scheme;
    const char *name;
  };
  const Codec codecs[] = {
    {COMPRESSION_ZSTD, "zstd"},
    {COMPRESSION_LZMA, "lzma"},
    {COMPRESSION_LZW, "lzw"},
    {COMPRESSION_ADOBE_DEFLATE, "deflate"},
  };

  constexpr uint32_t w = 16;
  constexpr uint32_t h = 16;
  std::vector<uint8_t> src(static_cast<size_t>(w) * h * 3);
  for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<uint8_t>((i * 37 + 11) & 0xFF);

  for (const auto &c : codecs) {
    const std::string path = scratch_path((std::string("tiff_codecs_") + c.name + ".tif").c_str());
    {
      TiffPtr tif = tiff_open(path, "w");
      ASSERT_NE(tif, nullptr) << c.name;
      TIFFSetField(tif.get(), TIFFTAG_IMAGEWIDTH, w);
      TIFFSetField(tif.get(), TIFFTAG_IMAGELENGTH, h);
      TIFFSetField(tif.get(), TIFFTAG_SAMPLESPERPIXEL, 3);
      TIFFSetField(tif.get(), TIFFTAG_BITSPERSAMPLE, 8);
      TIFFSetField(tif.get(), TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
      TIFFSetField(tif.get(), TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
      TIFFSetField(tif.get(), TIFFTAG_COMPRESSION, c.scheme);
      for (uint32_t row = 0; row < h; ++row) {
        ASSERT_EQ(TIFFWriteScanline(tif.get(), src.data() + static_cast<size_t>(row) * w * 3, row, 0), 1) << c.name;
      }
    }
    {
      TiffPtr tif = tiff_open(path, "r");
      ASSERT_NE(tif, nullptr) << c.name;
      uint16_t compression = 0;
      TIFFGetField(tif.get(), TIFFTAG_COMPRESSION, &compression);
      EXPECT_EQ(compression, c.scheme) << c.name;
      std::vector<uint8_t> out(src.size());
      for (uint32_t row = 0; row < h; ++row) {
        ASSERT_EQ(TIFFReadScanline(tif.get(), out.data() + static_cast<size_t>(row) * w * 3, row, 0), 1) << c.name;
      }
      EXPECT_EQ(src, out) << c.name;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }
}
