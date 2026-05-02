/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One-shot fixture generator for a minimal TIFF carrying EXIF rational-array
 * metadata (LensSpecification = RATIONAL[4]). The fixture exercises the
 * `case EXIF_DT_RATIONAL_PTR` branch in `SipiIOTiff::readExif()` — previously
 * dead code, activated by the `EXIFTAG_LENSSPECIFICATION` entry added to
 * `exiftag_list[]` in DEV-6356.
 *
 * Run **once** by a developer to regenerate the committed fixture under
 * `test/_test_data/images/unit/`. NOT invoked by CI. Commit both this source
 * file and the generated `.tif`.
 *
 * Build:
 *   clang++ -std=c++23 -O2 \
 *     -I$(brew --prefix libtiff)/include \
 *     -L$(brew --prefix libtiff)/lib -ltiff \
 *     generate_exif_rational_tiff.cpp -o generate_exif_rational_tiff
 *
 * Run (from the sipi repo root):
 *   ./generate_exif_rational_tiff test/_test_data/images/unit/
 */

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <tiffio.h>

namespace {

// LensSpecification per EXIF 2.31 §4.6.5: RATIONAL[4] =
//   [min focal length, max focal length, min F at min FL, min F at max FL]
// Values chosen to match a Nikkor 70-200 f/2.8 lens.
constexpr float kLensSpec[4] = {70.0f, 200.0f, 2.8f, 2.8f};

bool writeFixture(const std::filesystem::path &path)
{
  TIFF *tif = TIFFOpen(path.string().c_str(), "w");
  if (tif == nullptr) {
    std::fprintf(stderr, "TIFFOpen failed for %s\n", path.string().c_str());
    return false;
  }

  // Minimal 1x1 RGB image — pixel data is irrelevant; the test asserts on
  // EXIF metadata only. The single white pixel keeps the on-disk fixture
  // under 1 KB.
  const uint16_t kWidth = 1;
  const uint16_t kHeight = 1;
  const uint8_t kPixel[3] = {255, 255, 255};

  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(kWidth));
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(kHeight));
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(8));
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(3));
  TIFFSetField(tif, TIFFTAG_COMPRESSION, static_cast<uint16_t>(COMPRESSION_NONE));
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, static_cast<uint16_t>(PHOTOMETRIC_RGB));
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, static_cast<uint16_t>(PLANARCONFIG_CONTIG));
  TIFFSetField(tif, TIFFTAG_ORIENTATION, static_cast<uint16_t>(ORIENTATION_TOPLEFT));
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, static_cast<uint32_t>(kHeight));

  if (TIFFWriteScanline(tif, const_cast<uint8_t *>(kPixel), 0, 0) < 0) {
    std::fprintf(stderr, "TIFFWriteScanline failed\n");
    TIFFClose(tif);
    return false;
  }

  // Flush the main image directory before opening the EXIF SubIFD.
  if (!TIFFWriteDirectory(tif)) {
    std::fprintf(stderr, "TIFFWriteDirectory (main) failed\n");
    TIFFClose(tif);
    return false;
  }

  // Switch to a fresh EXIF directory and populate the rational-array tag.
  if (TIFFCreateEXIFDirectory(tif) != 0) {
    std::fprintf(stderr, "TIFFCreateEXIFDirectory failed\n");
    TIFFClose(tif);
    return false;
  }

  if (!TIFFSetField(tif, EXIFTAG_LENSSPECIFICATION, kLensSpec)) {
    std::fprintf(stderr, "TIFFSetField(EXIFTAG_LENSSPECIFICATION) failed\n");
    TIFFClose(tif);
    return false;
  }
  TIFFSetField(tif, EXIFTAG_LENSMAKE, "Nikon");
  TIFFSetField(tif, EXIFTAG_LENSMODEL, "AF-S Nikkor 70-200mm f/2.8E FL ED VR");

  uint64_t exif_dir_offset = 0;
  if (!TIFFWriteCustomDirectory(tif, &exif_dir_offset)) {
    std::fprintf(stderr, "TIFFWriteCustomDirectory (EXIF) failed\n");
    TIFFClose(tif);
    return false;
  }

  // Backpatch the SubIFD pointer on the main directory.
  if (!TIFFSetDirectory(tif, 0)) {
    std::fprintf(stderr, "TIFFSetDirectory(0) failed\n");
    TIFFClose(tif);
    return false;
  }
  TIFFSetField(tif, TIFFTAG_EXIFIFD, exif_dir_offset);
  if (!TIFFRewriteDirectory(tif)) {
    std::fprintf(stderr, "TIFFRewriteDirectory failed\n");
    TIFFClose(tif);
    return false;
  }

  TIFFClose(tif);
  std::printf("  wrote %s (LensSpecification=[%.1f,%.1f,%.1f,%.1f])\n",
    path.filename().string().c_str(),
    kLensSpec[0],
    kLensSpec[1],
    kLensSpec[2],
    kLensSpec[3]);
  return true;
}

}// namespace

int main(int argc, char **argv)
{
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <output_dir>\n", argv[0]);
    return 1;
  }

  const std::filesystem::path out_dir{argv[1]};
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);
  if (ec) {
    std::fprintf(stderr, "failed to create %s: %s\n", out_dir.string().c_str(), ec.message().c_str());
    return 1;
  }

  std::printf("Generating EXIF rational-array TIFF fixture under %s\n", out_dir.string().c_str());
  if (!writeFixture(out_dir / "exif_lens_specification.tif")) {
    std::fprintf(stderr, "\nfixture generation failed\n");
    return 2;
  }
  std::printf("\nFixture generated successfully.\n");
  return 0;
}
