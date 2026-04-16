/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One-shot fixture generator for synthetic 1-bit bilevel TIFF test fixtures.
 *
 * This program is run **once** by a developer to regenerate the committed
 * fixtures under `test/_test_data/images/bilevel/`. It is NOT invoked by the
 * normal CI build. Commit both this source file and the generated `.tif`
 * files so the fixtures are reproducible.
 *
 * Fixtures produced:
 *   - bilevel_lzw_miniswhite.tif   (bps=1, LZW,  PhotometricInterpretation::MINISWHITE)
 *   - bilevel_none_miniswhite.tif  (bps=1, None, PhotometricInterpretation::MINISWHITE)
 *   - bilevel_lzw_minisblack.tif   (bps=1, LZW,  PhotometricInterpretation::MINISBLACK)
 *   - bilevel_roi_test.tif         (bps=1, LZW,  MINISWHITE, designed for ROI testing)
 *
 * Build:
 *   clang++ -std=c++23 -O2 \
 *     -I$(brew --prefix libtiff)/include \
 *     -L$(brew --prefix libtiff)/lib -ltiff \
 *     generate_bilevel_tiffs.cpp -o generate_bilevel_tiffs
 *
 * Run (from the sipi repo root):
 *   ./generate_bilevel_tiffs test/_test_data/images/bilevel/
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <tiffio.h>
#include <vector>

namespace {

struct BilevelSpec
{
  std::string filename;
  uint32_t width;
  uint32_t height;
  uint16_t compression;///< COMPRESSION_NONE or COMPRESSION_LZW
  uint16_t photometric;///< PHOTOMETRIC_MINISWHITE or PHOTOMETRIC_MINISBLACK
  uint32_t rows_per_strip;
};

/*!
 * Produce a deterministic checker pattern so the fixture is visually verifiable.
 * Each packed byte encodes 8 pixels. The packed bit is set (= 1) when the pixel
 * is "on" in the photometric sense — with MINISWHITE that means a black pixel,
 * with MINISBLACK that means a white pixel. We write a 16x16 checkerboard.
 */
std::vector<uint8_t> makeCheckerScanline(uint32_t width, uint32_t row, uint32_t block = 16)
{
  const uint32_t bytes = (width + 7) / 8;
  std::vector<uint8_t> out(bytes, 0);
  const bool row_block_on = ((row / block) % 2) == 0;
  for (uint32_t x = 0; x < width; ++x) {
    const bool col_block_on = ((x / block) % 2) == 0;
    const bool pixel_on = row_block_on ^ col_block_on;
    if (pixel_on) {
      out[x / 8] |= (uint8_t)(0x80 >> (x % 8));
    }
  }
  return out;
}

bool writeBilevelTiff(const std::filesystem::path &path, const BilevelSpec &spec)
{
  TIFF *tif = TIFFOpen(path.string().c_str(), "w");
  if (tif == nullptr) {
    std::fprintf(stderr, "TIFFOpen failed for %s\n", path.string().c_str());
    return false;
  }

  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, spec.width);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, spec.height);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(1));
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(1));
  TIFFSetField(tif, TIFFTAG_COMPRESSION, spec.compression);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, spec.photometric);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, static_cast<uint16_t>(PLANARCONFIG_CONTIG));
  TIFFSetField(tif, TIFFTAG_ORIENTATION, static_cast<uint16_t>(ORIENTATION_TOPLEFT));
  TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, static_cast<uint16_t>(RESUNIT_INCH));
  TIFFSetField(tif, TIFFTAG_XRESOLUTION, 300.0f);
  TIFFSetField(tif, TIFFTAG_YRESOLUTION, 300.0f);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, spec.rows_per_strip);

  for (uint32_t row = 0; row < spec.height; ++row) {
    auto scanline = makeCheckerScanline(spec.width, row);
    if (TIFFWriteScanline(tif, scanline.data(), row, 0) < 0) {
      std::fprintf(stderr, "TIFFWriteScanline failed for %s at row %u\n", path.string().c_str(), row);
      TIFFClose(tif);
      return false;
    }
  }

  TIFFClose(tif);
  std::printf("  wrote %s (%ux%u, %s, %s)\n",
    path.filename().string().c_str(),
    spec.width,
    spec.height,
    spec.compression == COMPRESSION_LZW ? "LZW" : "none",
    spec.photometric == PHOTOMETRIC_MINISWHITE ? "miniswhite" : "minisblack");
  return true;
}

}// namespace

int main(int argc, char **argv)
{
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <output_dir>\n", argv[0]);
    return 1;
  }

  const std::filesystem::path out_dir{ argv[1] };
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);
  if (ec) {
    std::fprintf(stderr, "failed to create %s: %s\n", out_dir.string().c_str(), ec.message().c_str());
    return 1;
  }

  // Match the characteristics of the real DEV-6249 files (CanoScan 9000F +
  // Photoshop CC, bps=1, LZW, MINISWHITE) but at a tractable size so the
  // fixture bytes are small enough to commit.
  const std::vector<BilevelSpec> specs{
    { "bilevel_lzw_miniswhite.tif", 128, 128, COMPRESSION_LZW, PHOTOMETRIC_MINISWHITE, 64 },
    { "bilevel_none_miniswhite.tif", 128, 128, COMPRESSION_NONE, PHOTOMETRIC_MINISWHITE, 64 },
    { "bilevel_lzw_minisblack.tif", 128, 128, COMPRESSION_LZW, PHOTOMETRIC_MINISBLACK, 64 },
    // The ROI test fixture is slightly larger and uses multi-strip layout so
    // the IIIF region-crop path exercises non-trivial offsets and strip seeks.
    { "bilevel_roi_test.tif", 256, 192, COMPRESSION_LZW, PHOTOMETRIC_MINISWHITE, 48 },
  };

  std::printf("Generating %zu bilevel TIFF fixtures under %s\n", specs.size(), out_dir.string().c_str());
  int failures = 0;
  for (const auto &spec : specs) {
    if (!writeBilevelTiff(out_dir / spec.filename, spec)) {
      ++failures;
    }
  }
  if (failures > 0) {
    std::fprintf(stderr, "\n%d fixture(s) failed\n", failures);
    return 2;
  }
  std::printf("\nAll fixtures generated successfully.\n");
  return 0;
}
