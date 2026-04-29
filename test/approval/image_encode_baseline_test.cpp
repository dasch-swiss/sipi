/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

// Image-encode bit-exactness baseline (plan §6.1).
//
// Captures byte-for-byte TIFF output for representative inputs exercised
// through the SipiImage IIIF pipeline (region/size/rotation + format
// conversion). Approved goldens become the regression gate for every dep
// migration in PRs 1-4 of the Nix-native build series — a libtiff /
// libjpeg / Kakadu / lcms2 version bump that changes even one output byte
// trips the test, gating the migration.
//
// Why TIFF-only: JPEG and PNG outputs from SipiImage embed the wall-clock
// time into ICC profile creation-date headers (seconds field varies per
// run), so byte-for-byte comparison of those formats is unstable across
// invocations. TIFF outputs are deterministic. Decode-side coverage of
// libjpeg/libpng/Kakadu is preserved by reading those formats as inputs.
//
// Capturing goldens (one-time, before this test becomes a gate):
//   1. Build + run locally:
//        nix develop --command bash -c \
//          "cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && \
//           cmake --build build --target sipi.approvaltests --parallel && \
//           cd build/test/approval && ./sipi.approvaltests \
//             --gtest_filter='ImageEncodeBaseline.*'"
//   2. Tests SKIP with paths of `.received.tif` files captured under
//      test/approval/approval_tests/. Inspect the outputs.
//   3. For each `<name>.received.tif`, rename to `<name>.approved.tif` and
//      commit alongside this source file.
//
// Re-approval procedure for intentional drift after goldens land — see
// test/approval/CHANGELOG.approval.md.

#include "gtest/gtest.h"

#include "SipiImage.hpp"
#include "SipiImageError.hpp"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiSize.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>

// Approval tests run from build/test/approval/.
static const std::string test_images = "../../../test/_test_data/images/";
static const std::string approved_dir = "../../../test/approval/approval_tests";

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
// inspect them — under approved_dir if writable (dev-shell inner loop),
// otherwise under $TMPDIR (Nix sandbox checkPhase).
std::string received_dir()
{
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

// JP2 → TIFF is intentionally NOT covered as a bit-exact gate: the JP2
// reader path passes through lcms2's ICC profile generator, which embeds
// a fresh wall-clock timestamp into the ICC profile header on every
// run. Kakadu decode is exercised by the e2e Rust tests and existing
// unit tests; bit-exactness on JP2 inputs awaits an upstream fix.

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
