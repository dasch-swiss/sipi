/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

// Image-encode bit-exactness baseline.
//
// Captures byte-for-byte output across TIFF / JPEG / PNG / JP2 for
// representative inputs exercised through the SipiImage IIIF pipeline
// (region / size / rotation + format conversion). Approved goldens are
// the regression gate for every dep migration — a libtiff / libjpeg /
// libpng / Kakadu / lcms2 version bump that changes even one output byte
// trips the test, gating the migration.
//
// Determinism gate. JPEG / PNG / JP2-decode outputs go through lcms2's
// profile generator, which stamps wall-clock UTC into ICC bytes 24-35.
// SipiIcc::iccBytes() rewrites those bytes (and zeros the Profile ID at
// bytes 84-99) when SOURCE_DATE_EPOCH is set. CMake injects the env var
// for sipi.approvaltests via set_tests_properties, so byte-for-byte
// comparison is stable under ctest. Production never sets the var and
// retains lcms2's wall-clock behaviour. See docs/adr/0002-icc-profile-
// determinism-test-only.md and test/approval/CHANGELOG.approval.md.
//
// Capturing goldens (one-time, before a test becomes a gate):
//   1. Build + run locally with the env var injected:
//        nix develop --command bash -c \
//          "cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && \
//           cmake --build build --target sipi.approvaltests --parallel && \
//           cd build/test/approval && SOURCE_DATE_EPOCH=946684800 \
//             ./sipi.approvaltests --gtest_filter='ImageEncodeBaseline.*'"
//   2. Tests SKIP with paths of `.received.<ext>` files captured under
//      test/approval/approval_tests/. Inspect the outputs.
//   3. For each `<name>.received.<ext>`, rename to `<name>.approved.<ext>`,
//      track in Git LFS, and commit alongside this source file.
//
// Re-approval procedure for intentional drift after goldens land — see
// test/approval/CHANGELOG.approval.md.

#include "gtest/gtest.h"

#include "SipiImage.hpp"
#include "SipiImageError.hpp"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiSize.h"
#include "test_paths.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>

// `sipi::test::workspace_path()` honours Bazel's SIPI_WORKSPACE_ROOT env
// (cc_test cwd = workspace root in runfiles). The CMake fallback
// `"../../.."` is the 3-level traversal from build/test/approval/ to
// the workspace root. See test/test_paths.hpp.
static const std::string test_images = sipi::test::workspace_path("test/_test_data/images", "../../..") + "/";
static const std::string approved_dir = sipi::test::workspace_path("test/approval/approval_tests", "../../..");

namespace {

bool file_exists(const std::string &path)
{
  struct stat buf{};
  return stat(path.c_str(), &buf) == 0;
}

std::string slurp(const std::string &path)
{
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void write_bytes(const std::string &path, const std::string &bytes)
{
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream out(path, std::ios::binary);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Encode an image through the SipiImage pipeline with optional IIIF
// region/size/rotation, then write to `out_path` in the requested format.
void encode(const std::string &in_path,
  const std::string &out_path,
  const std::string &format,
  const std::shared_ptr<Sipi::SipiRegion> &region,
  const std::shared_ptr<Sipi::SipiSize> &size,
  float rotation = 0.0f,
  bool mirror = false)
{
  Sipi::SipiImage img;
  img.read(in_path, region, size);
  if (rotation != 0.0f || mirror) img.rotate(rotation, mirror);
  img.write(format, out_path);
}

// Where to drop `.received.<ext>` files when the maintainer needs to
// inspect them — TEST_TMPDIR (Bazel cc_test, written under
// `bazel-testlogs/`), under approved_dir if writable (CMake dev-shell
// inner loop), otherwise under $TMPDIR (Nix sandbox checkPhase).
std::string received_dir()
{
  if (const char *bazel_tmp = std::getenv("TEST_TMPDIR")) { return std::string(bazel_tmp); }
  std::error_code ec;
  std::filesystem::create_directories(approved_dir, ec);
  if (!ec) return approved_dir;
  const char *tmp = std::getenv("TMPDIR");
  return std::string(tmp ? tmp : "/tmp") + "/sipi_baseline_received";
}

// Compare the output file to its approved golden. If the golden does not
// exist yet, write a `.received.<ext>` file and SKIP with capture
// instructions so a maintainer can rename → `.approved.<ext>` and commit.
void verify_or_capture(const std::string &output_path, const std::string &golden_name)
{
  std::string ext = std::filesystem::path(output_path).extension().string();
  std::string approved_path = approved_dir + "/" + golden_name + ".approved" + ext;
  std::string received_path = received_dir() + "/" + golden_name + ".received" + ext;

  std::string output_bytes = slurp(output_path);
  std::remove(output_path.c_str());

  if (!file_exists(approved_path)) {
    write_bytes(received_path, output_bytes);
    GTEST_SKIP() << "No approved golden for " << golden_name
                 << ". Wrote " << received_path << " (" << output_bytes.size()
                 << " bytes). Rename to " << golden_name << ".approved" << ext
                 << " under test/approval/approval_tests/ to activate the regression gate.";
  }

  std::string approved_bytes = slurp(approved_path);
  if (output_bytes != approved_bytes) {
    write_bytes(received_path, output_bytes);
    FAIL() << "Byte mismatch vs " << approved_path
           << ". Output: " << output_bytes.size() << " bytes, golden: "
           << approved_bytes.size() << " bytes. Wrote received bytes to "
           << received_path
           << " for inspection. See test/approval/CHANGELOG.approval.md "
              "for the re-approval procedure.";
  }
  std::remove(received_path.c_str());// passed → drop stale received file
}

}// namespace

// All outputs are downscaled or region-cropped to keep goldens small
// (committed to the repo). The regression gate is sensitive to encoder
// drift regardless of output size — a one-byte change in libtiff still
// trips the test.

// JPEG decode + rescale + TIFF encode — covers libjpeg decode path.
TEST(ImageEncodeBaseline, JpegToTiffDownscaled)
{
  std::string in = test_images + "unit/MaoriFigure.jpg";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_jpeg_to_tiff_downscaled.tif";
  encode(in, out, "tif", nullptr, std::make_shared<Sipi::SipiSize>("256,"));
  verify_or_capture(out, "ImageEncodeBaseline.JpegToTiffDownscaled");
}

// TIFF decode + region crop + TIFF encode — libtiff round-trip.
TEST(ImageEncodeBaseline, TiffRegionRoundTrip)
{
  std::string in = test_images + "unit/lena512.tif";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_tiff_region_roundtrip.tif";
  auto region = std::make_shared<Sipi::SipiRegion>(0, 0, 256, 256);
  encode(in, out, "tif", region, nullptr);
  verify_or_capture(out, "ImageEncodeBaseline.TiffRegionRoundTrip");
}

// CMYK TIFF -> downscaled TIFF — exercises the CMYK colour-space path.
TEST(ImageEncodeBaseline, CmykTiffDownscaled)
{
  std::string in = test_images + "unit/cmyk.tif";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_cmyk_downscaled.tif";
  encode(in, out, "tif", nullptr, std::make_shared<Sipi::SipiSize>("256,"));
  verify_or_capture(out, "ImageEncodeBaseline.CmykTiffDownscaled");
}

// CIELab TIFF -> downscaled TIFF — exercises the CIELab colour-space path.
TEST(ImageEncodeBaseline, CielabTiffDownscaled)
{
  std::string in = test_images + "unit/cielab.tif";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_cielab_downscaled.tif";
  encode(in, out, "tif", nullptr, std::make_shared<Sipi::SipiSize>("256,"));
  verify_or_capture(out, "ImageEncodeBaseline.CielabTiffDownscaled");
}

// JPEG decode + 90-degree rotation + downscaled TIFF — rotation path.
TEST(ImageEncodeBaseline, JpegRotatedDownscaledTiff)
{
  std::string in = test_images + "unit/MaoriFigure.jpg";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_jpeg_rotated_downscaled.tif";
  encode(in, out, "tif", nullptr, std::make_shared<Sipi::SipiSize>("256,"), 90.0f);
  verify_or_capture(out, "ImageEncodeBaseline.JpegRotatedDownscaledTiff");
}

// ---------------------------------------------------------------------------
// JPEG / PNG / JP2 outputs — gated on SOURCE_DATE_EPOCH being set so lcms2's
// wall-clock-stamped ICC creation date is overwritten with a fixed value.
// CMake injects the env var for the ctest-driven invocation; running this
// binary directly requires `SOURCE_DATE_EPOCH=946684800 ./sipi.approvaltests`.
// ---------------------------------------------------------------------------

// JPEG decode + downscale + JPEG encode — exercises the lcms2 ICC emit
// path on the libjpeg writer.
TEST(ImageEncodeBaseline, JpegFullToJpegDownscaled)
{
  std::string in = test_images + "unit/MaoriFigureWatermark.jpg";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_jpeg_full_to_jpeg_downscaled.jpg";
  encode(in, out, "jpg", nullptr, std::make_shared<Sipi::SipiSize>("256,"));
  verify_or_capture(out, "ImageEncodeBaseline.JpegFullToJpegDownscaled");
}

// JPEG decode + downscale + PNG encode — exercises the lcms2 ICC emit
// path on the libpng writer.
TEST(ImageEncodeBaseline, JpegFullToPng)
{
  std::string in = test_images + "unit/MaoriFigureWatermark.jpg";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_jpeg_full_to_png.png";
  encode(in, out, "png", nullptr, std::make_shared<Sipi::SipiSize>("256,"));
  verify_or_capture(out, "ImageEncodeBaseline.JpegFullToPng");
}

// JP2 decode + region crop + TIFF encode — Kakadu decode + libtiff encode.
// JP2 decode synthesizes an ICC profile via lcms2 (the path that needs
// SOURCE_DATE_EPOCH for byte-exactness).
TEST(ImageEncodeBaseline, J2kRegionToTiff)
{
  std::string in = test_images + "unit/gray_with_icc.jp2";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_j2k_region_to_tiff.tif";
  auto region = std::make_shared<Sipi::SipiRegion>(0, 0, 256, 256);
  encode(in, out, "tif", region, nullptr);
  verify_or_capture(out, "ImageEncodeBaseline.J2kRegionToTiff");
}

// JP2 decode + region crop + JPEG encode — Kakadu decode + libjpeg encode.
TEST(ImageEncodeBaseline, J2kRegionToJpeg)
{
  std::string in = test_images + "unit/gray_with_icc.jp2";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_j2k_region_to_jpeg.jpg";
  auto region = std::make_shared<Sipi::SipiRegion>(0, 0, 256, 256);
  encode(in, out, "jpg", region, nullptr);
  verify_or_capture(out, "ImageEncodeBaseline.J2kRegionToJpeg");
}

// JP2 decode + region crop + JP2 encode — the Goal-3 determinism check.
// Kakadu's jp2_colour::init() embeds the (already-normalized) ICC bytes;
// this golden verifies the encoder doesn't reframe them.
TEST(ImageEncodeBaseline, J2kRegionToJp2)
{
  std::string in = test_images + "unit/gray_with_icc.jp2";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_j2k_region_to_jp2.jp2";
  auto region = std::make_shared<Sipi::SipiRegion>(0, 0, 256, 256);
  encode(in, out, "jpx", region, nullptr);
  verify_or_capture(out, "ImageEncodeBaseline.J2kRegionToJp2");
}

// CMYK TIFF -> downscaled JPEG — exercises CMYK -> sRGB lcms2 colour
// conversion + libjpeg encode.
TEST(ImageEncodeBaseline, CmykTiffToJpeg)
{
  std::string in = test_images + "unit/cmyk.tif";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_cmyk_tiff_to_jpeg.jpg";
  encode(in, out, "jpg", nullptr, std::make_shared<Sipi::SipiSize>("256,"));
  verify_or_capture(out, "ImageEncodeBaseline.CmykTiffToJpeg");
}

// CIELab TIFF -> downscaled JPEG — exercises CIELab -> sRGB lcms2 colour
// conversion + libjpeg encode.
TEST(ImageEncodeBaseline, CielabTiffToJpeg)
{
  std::string in = test_images + "unit/cielab.tif";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_cielab_tiff_to_jpeg.jpg";
  encode(in, out, "jpg", nullptr, std::make_shared<Sipi::SipiSize>("256,"));
  verify_or_capture(out, "ImageEncodeBaseline.CielabTiffToJpeg");
}

// JPEG decode + 90-degree rotation + JPEG encode — rotation path on the
// lossy encoder.
TEST(ImageEncodeBaseline, JpegRotatedToJpeg)
{
  std::string in = test_images + "unit/MaoriFigureWatermark.jpg";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_jpeg_rotated_to_jpeg.jpg";
  encode(in, out, "jpg", nullptr, std::make_shared<Sipi::SipiSize>("256,"), 90.0f);
  verify_or_capture(out, "ImageEncodeBaseline.JpegRotatedToJpeg");
}

// PNG decode + downscale + PNG encode — libpng round-trip through the
// lcms2 ICC emit path. Fixture is a PNG-with-embedded-ICC committed
// alongside the goldens.
TEST(ImageEncodeBaseline, PngRoundTrip)
{
  std::string in = test_images + "unit/sample_with_icc.png";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_png_round_trip.png";
  encode(in, out, "png", nullptr, std::make_shared<Sipi::SipiSize>("256,"));
  verify_or_capture(out, "ImageEncodeBaseline.PngRoundTrip");
}

// CIELab TIFF -> downscaled PNG — libtiff decode + libpng encode +
// lcms2 ICC emit.
TEST(ImageEncodeBaseline, TiffToPng)
{
  std::string in = test_images + "unit/cielab.tif";
  if (!file_exists(in)) GTEST_SKIP() << "Test image not found: " << in;
  std::string out = "/tmp/sipi_baseline_tiff_to_png.png";
  encode(in, out, "png", nullptr, std::make_shared<Sipi::SipiSize>("256,"));
  verify_or_capture(out, "ImageEncodeBaseline.TiffToPng");
}
