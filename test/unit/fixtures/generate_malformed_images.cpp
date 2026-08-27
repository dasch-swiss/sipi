/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One-shot fixture generator for the crafted malformed-image regression
 * suite. Run by a developer to (re)generate the committed fixtures under
 * `test/_test_data/images/malformed/`; not invoked by the normal CI build.
 * Commit both this source file and any generated image files (via Git LFS)
 * so the fixtures are reproducible.
 *
 * Each fixture is produced by its own emit function, added to `kGenerators`
 * below as it lands.
 *
 * Run:
 *   bazel run //test/unit/fixtures:generate_malformed_images -- \
 *     test/_test_data/images/malformed/
 */

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <tiffio.h>

namespace {

/*!
 * Signature every per-fixture emit function follows: write one malformed
 * fixture into `out_dir` and return true on success. Add new emit functions
 * above `main` and register them in `kGenerators`.
 */
using GeneratorFn = bool (*)(const std::filesystem::path &out_dir);

// Extension point: later changes append `{ "defect_name", &emitDefectName }`
// entries here, one per malformed fixture.
struct Generator
{
  std::string name;
  GeneratorFn emit;
};

/*!
 * TIFF header that claims a width larger than SipiIO's kMaxDecodeDim
 * (1 << 17). No pixel strips are written — validate_decode_dims() rejects
 * the file from the header alone, before any pixel data would be touched, so
 * this fixture stays tiny (DEV-6063).
 */
bool emitTiffOversizedDimensions(const std::filesystem::path &out_dir)
{
  const auto path = out_dir / "tiff_oversized_dimensions.tif";
  TIFF *tif = TIFFOpen(path.string().c_str(), "w");
  if (tif == nullptr) {
    std::fprintf(stderr, "TIFFOpen failed for %s\n", path.string().c_str());
    return false;
  }

  // kMaxDecodeDim is 1 << 17 (131072); claim a width well beyond it.
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(1u << 20));
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(8));
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(8));
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(1));
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, static_cast<uint16_t>(PHOTOMETRIC_MINISBLACK));
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, static_cast<uint16_t>(PLANARCONFIG_CONTIG));
  TIFFSetField(tif, TIFFTAG_COMPRESSION, static_cast<uint16_t>(COMPRESSION_NONE));
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, static_cast<uint32_t>(8));
  // No TIFFWriteScanline calls: we only need a directory whose IMAGEWIDTH
  // field lies about the raster size.
  TIFFWriteDirectory(tif);
  TIFFClose(tif);
  std::printf("  wrote %s (declared 1048576x8, no pixel data)\n", path.filename().string().c_str());
  return true;
}

/*!
 * A palette (indexed-color) TIFF whose BitsPerSample (32) falls outside the
 * set SipiIOTiff supports ({1, 4, 8, 12, 16}). validate_decode_dims()
 * rejects this before the colormap length (2^bps) is ever computed (N6);
 * without the bps allowlist, `1 << 32` would be undefined behavior on a
 * 32-bit `int` shift.
 */
bool emitTiffColormapUnsupportedBps(const std::filesystem::path &out_dir)
{
  const auto path = out_dir / "tiff_colormap_unsupported_bps.tif";
  TIFF *tif = TIFFOpen(path.string().c_str(), "w");
  if (tif == nullptr) {
    std::fprintf(stderr, "TIFFOpen failed for %s\n", path.string().c_str());
    return false;
  }

  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(4));
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(1));
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(32));
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(1));
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, static_cast<uint16_t>(PHOTOMETRIC_PALETTE));
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, static_cast<uint16_t>(PLANARCONFIG_CONTIG));
  TIFFSetField(tif, TIFFTAG_COMPRESSION, static_cast<uint16_t>(COMPRESSION_NONE));
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, static_cast<uint32_t>(1));
  // No ColorMap tag: validate_decode_dims() must reject the bps before
  // SipiIOTiff::read() ever asks libtiff for TIFFTAG_COLORMAP.
  TIFFWriteDirectory(tif);
  TIFFClose(tif);
  std::printf("  wrote %s (bps=32, palette, no colormap)\n", path.filename().string().c_str());
  return true;
}

/*!
 * Well-formed grayscale TIFF colorimetry fixture: WhitePoint +
 * TransferFunction set, no embedded ICC profile, forcing SipiIOTiff::read()
 * down the TIFFTAG_TRANSFERFUNCTION decode path (N3). This is a regression
 * fixture for the corrected variadic TIFFGetField call (one uint16_t*
 * out-pointer for a grayscale/SamplesPerPixel==1 image, `1 << bps` entries) —
 * a successful, crash-free decode is the expected outcome, not a thrown
 * error.
 */
bool emitTiffTransferFunctionGrayscale(const std::filesystem::path &out_dir)
{
  const auto path = out_dir / "tiff_transferfunction_grayscale.tif";
  TIFF *tif = TIFFOpen(path.string().c_str(), "w");
  if (tif == nullptr) {
    std::fprintf(stderr, "TIFFOpen failed for %s\n", path.string().c_str());
    return false;
  }

  constexpr uint32_t w = 8;
  constexpr uint32_t h = 8;
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, w);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, h);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(8));
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(1));
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, static_cast<uint16_t>(PHOTOMETRIC_MINISBLACK));
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, static_cast<uint16_t>(PLANARCONFIG_CONTIG));
  TIFFSetField(tif, TIFFTAG_COMPRESSION, static_cast<uint16_t>(COMPRESSION_NONE));
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, h);

  float whitepoint[2] = { 0.3127f, 0.3290f };// D65
  TIFFSetField(tif, TIFFTAG_WHITEPOINT, whitepoint);

  // SamplesPerPixel (1) - ExtraSamples (0) == 1: grayscale, libtiff expects
  // exactly one uint16_t* out-pointer to a 1 << bps (256) entry table.
  uint16_t table[256];
  for (int i = 0; i < 256; ++i) { table[i] = static_cast<uint16_t>(i * 257); }// identity ramp, 0..65535
  TIFFSetField(tif, TIFFTAG_TRANSFERFUNCTION, table);

  std::vector<uint8_t> row(w);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) { row[x] = static_cast<uint8_t>((x + y) * 16); }
    if (TIFFWriteScanline(tif, row.data(), y, 0) < 0) {
      std::fprintf(stderr, "TIFFWriteScanline failed for %s at row %u\n", path.string().c_str(), y);
      TIFFClose(tif);
      return false;
    }
  }

  TIFFClose(tif);
  std::printf("  wrote %s (%ux%u, grayscale, WhitePoint + TransferFunction)\n",
    path.filename().string().c_str(),
    w,
    h);
  return true;
}

/*!
 * A 1-bit-per-sample palette (indexed-color) TIFF with a 2-entry colormap.
 * SipiIOTiff's sub-8-bit palette unpacking (one2eight()) does not special-
 * case PhotometricInterpretation::PALETTE the way the 4-bit and 12-bit
 * unpackers do, so the decoded index byte is not guaranteed to stay within
 * [0, colormap_len) for every possible bit pattern. This fixture exercises
 * the validate-before-lookup guard added at the palette-to-RGB expansion
 * step (DEV-6065): a well-formed decode must either produce in-range
 * indices or the read must fail closed with SipiImageError, never read past
 * the colormap tables.
 */
bool emitTiffPaletteOneBit(const std::filesystem::path &out_dir)
{
  const auto path = out_dir / "tiff_palette_1bit.tif";
  TIFF *tif = TIFFOpen(path.string().c_str(), "w");
  if (tif == nullptr) {
    std::fprintf(stderr, "TIFFOpen failed for %s\n", path.string().c_str());
    return false;
  }

  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(8));
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(1));
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(1));
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(1));
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, static_cast<uint16_t>(PHOTOMETRIC_PALETTE));
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, static_cast<uint16_t>(PLANARCONFIG_CONTIG));
  TIFFSetField(tif, TIFFTAG_COMPRESSION, static_cast<uint16_t>(COMPRESSION_NONE));
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, static_cast<uint32_t>(1));

  uint16_t r[2] = { 0, 65535 };
  uint16_t g[2] = { 0, 0 };
  uint16_t b[2] = { 0, 0 };
  TIFFSetField(tif, TIFFTAG_COLORMAP, r, g, b);

  uint8_t row[1] = { 0b10101010 };
  if (TIFFWriteScanline(tif, row, 0, 0) < 0) {
    std::fprintf(stderr, "TIFFWriteScanline failed for %s\n", path.string().c_str());
    TIFFClose(tif);
    return false;
  }

  TIFFClose(tif);
  std::printf("  wrote %s (8x1, 1-bit palette, 2-entry colormap)\n", path.filename().string().c_str());
  return true;
}

const std::vector<Generator> kGenerators{
  { "oversized_dimensions", &emitTiffOversizedDimensions },
  { "colormap_unsupported_bps", &emitTiffColormapUnsupportedBps },
  { "transferfunction_grayscale", &emitTiffTransferFunctionGrayscale },
  { "palette_1bit", &emitTiffPaletteOneBit },
};

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

  std::printf("Generating %zu malformed-image fixture(s) under %s\n", kGenerators.size(), out_dir.string().c_str());
  int failures = 0;
  for (const auto &generator : kGenerators) {
    if (!generator.emit(out_dir)) {
      std::fprintf(stderr, "  failed: %s\n", generator.name.c_str());
      ++failures;
    } else {
      std::printf("  wrote %s\n", generator.name.c_str());
    }
  }
  if (failures > 0) {
    std::fprintf(stderr, "\n%d fixture(s) failed\n", failures);
    return 2;
  }

  std::printf("\nAll fixtures generated successfully.\n");
  return 0;
}
