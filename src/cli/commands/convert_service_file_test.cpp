/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

// Service File command unit tests (DEV-6537 Phase 12.5).
//
// Exercises the command behind `sipi convert service-file <in> <out>`:
// reads a synthetic source, applies the command's transformations, and
// inspects the Essentials packet embedded in the output. Covers the
// invariants the command promises per ADR-0010 + the D5 option matrix:
//
//   * Identity fields (`origname`, `mimetype`) reflect the source.
//   * Image-shape fields and `data_chksum` reflect the **post-transformation**
//     pixel state, not the source state. With `--topleft` on a 90°-rotated
//     EXIF source, dimensions swap and the hash diverges from the source.
//   * Any Essentials the source happened to carry is dropped — the output
//     is keyed to the new source path, not the upstream one.
//   * Bad output extensions and missing inputs surface as non-zero exits
//     (boundary validation at the CLI seam).
//
// Test harness notes:
//   * `SipiIOTiff::initLibrary()` is called once via a global testing
//     environment — it registers TIFFTAG_SIPIMETA_PB with libtiff via
//     `TIFFSetTagExtender`. Production sipi calls this from `main()`;
//     tests need to do it explicitly or the tag round-trips silently no-op.
//   * Bazel exposes test fixtures via symlinks, so libmagic — used by
//     `shttps::Parsing::getFileMimetype` to populate `EssentialsFields::mimetype`
//     — reports "inode/symlink" when handed the runfiles path. Tests copy
//     fixtures into TEST_TMPDIR so the production code path sees a real
//     file (mirrors how operators invoke the CLI).

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <ios>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "SipiImage.hpp"
#include "cli/commands/convert_service_file.h"
#include "SipiIOTiff.h"
#include "metadata/essentials.h"
#include "shttps/Hash.h"
#include "test_paths.hpp"

namespace {

const std::string test_images = sipi::test::data_dir() + "/images/";
const std::string tmp_dir = sipi::test::tmp_dir() + "/";

// Global init — register sipi's custom libtiff fields (TIFFTAG_SIPIMETA_PB
// and friends) so the command's TIFF writer can emit the Essentials
// carrier tag. Production sipi runs this from `main()` in `src/cli/sipi.cpp`;
// the unit-test runner links against gtest_main, so we register an
// Environment that does the same before any test runs. `initLibrary` is
// idempotent (guarded by a static-local `done` flag).
struct SipiIOInit : ::testing::Environment
{
  void SetUp() override { Sipi::SipiIOTiff::initLibrary(); }
};
[[maybe_unused]] auto *kSipiIOInit = ::testing::AddGlobalTestEnvironment(new SipiIOInit);

bool file_exists(const std::string &path)
{
  struct stat buf {};
  return stat(path.c_str(), &buf) == 0;
}

// Materialize a runfiles fixture into TEST_TMPDIR as a real file. Bazel
// links fixtures into runfiles via symlinks; libmagic (used by
// `getFileMimetype`) reports "inode/symlink" for those, which would
// poison the command's `EssentialsFields::mimetype` stamping under
// test. Operators invoke the CLI with real files, so the test should too.
std::string materialize_fixture(const std::string &fixture_relpath, const std::string &dst_name)
{
  const std::string src = test_images + fixture_relpath;
  const std::string dst = tmp_dir + dst_name;
  std::ifstream in(src, std::ios::binary);
  std::ofstream out(dst, std::ios::binary | std::ios::trunc);
  out << in.rdbuf();
  return dst;
}

// Read an existing Service File and pull its Essentials packet out. The
// command writes the packet into the carrier (JP2 UUID box / TIFF tag
// 65112); SipiImage::read routes through the format-handler reader, which
// surfaces the parsed `Essentials` on the image. For our purposes the
// resulting in-memory packet is the command's externally-visible output.
Sipi::EssentialsFields read_back_essentials(const std::string &path)
{
  Sipi::SipiImage img;
  img.read(path);
  const Sipi::Essentials &es = img.essential_metadata();
  EXPECT_TRUE(es.is_set()) << "Expected Essentials packet in " << path;
  return es.fields();
}

}// namespace

// 12.5 — Happy path: lena512.tif → JP2. The command should emit a
// Service File with an Essentials packet whose identity fields name the
// source, shape fields match the post-read dimensions, and `data_chksum`
// is a 32-byte SHA-256 of the post-transformation pixels.
TEST(CmdConvertServiceFile, EmitsEssentialsPacketForJp2)
{
  const std::string src = materialize_fixture("unit/lena512.tif", "_sfo_src_lena.tif");
  const std::string dst = tmp_dir + "_sfo_jp2.jp2";
  ASSERT_TRUE(file_exists(src)) << "Test image not found: " << src;

  Sipi::cli::ConvertServiceFileArgs req;
  req.input_path = src;
  req.output_path = dst;
  req.set_topleft = false;

  ASSERT_EQ(Sipi::cli::cmd_convert_service_file(req), EXIT_SUCCESS);
  ASSERT_TRUE(file_exists(dst));

  const auto f = read_back_essentials(dst);
  EXPECT_EQ(f.origname, "_sfo_src_lena.tif") << "origname must be source basename, not full path";
  EXPECT_EQ(f.mimetype, "image/tiff") << "mimetype must reflect the SOURCE format";
  EXPECT_EQ(f.hash_type, shttps::HashType::sha256);
  EXPECT_EQ(f.data_chksum.size(), 32u) << "SHA-256 digest is 32 bytes";
  EXPECT_EQ(f.img_w, 512u);
  EXPECT_EQ(f.img_h, 512u);
  EXPECT_GT(f.nc, 0u);
  EXPECT_GT(f.bps, 0u);
}

// 12.5 — Happy path: lena512.tif → pyramidal TIFF. Same invariants as JP2;
// the carrier is the TIFFTAG_SIPIMETA_PB tag instead of a UUID box.
TEST(CmdConvertServiceFile, EmitsEssentialsPacketForPyramidalTiff)
{
  const std::string src = materialize_fixture("unit/lena512.tif", "_sfo_src_lena_tiff.tif");
  const std::string dst = tmp_dir + "_sfo_tiff.tif";
  ASSERT_TRUE(file_exists(src)) << "Test image not found: " << src;

  Sipi::cli::ConvertServiceFileArgs req;
  req.input_path = src;
  req.output_path = dst;
  req.set_topleft = false;

  ASSERT_EQ(Sipi::cli::cmd_convert_service_file(req), EXIT_SUCCESS);
  ASSERT_TRUE(file_exists(dst));

  const auto f = read_back_essentials(dst);
  EXPECT_EQ(f.origname, "_sfo_src_lena_tiff.tif");
  EXPECT_EQ(f.mimetype, "image/tiff");
  EXPECT_EQ(f.hash_type, shttps::HashType::sha256);
  EXPECT_EQ(f.data_chksum.size(), 32u);
  EXPECT_EQ(f.img_w, 512u);
  EXPECT_EQ(f.img_h, 512u);
}

// 12.5 — Post-transformation hash matches the actual pixel bytes of the
// emitted Service File. Re-decoding the output and recomputing the SHA-256
// must equal the `data_chksum` the command stamped — this is the
// corruption-tripwire contract that `verify service-file` later relies on.
TEST(CmdConvertServiceFile, DataChksumMatchesEmittedPixels)
{
  const std::string src = materialize_fixture("unit/lena512.tif", "_sfo_src_hash.tif");
  const std::string dst = tmp_dir + "_sfo_hash.jp2";
  ASSERT_TRUE(file_exists(src)) << "Test image not found: " << src;

  Sipi::cli::ConvertServiceFileArgs req;
  req.input_path = src;
  req.output_path = dst;

  ASSERT_EQ(Sipi::cli::cmd_convert_service_file(req), EXIT_SUCCESS);
  ASSERT_TRUE(file_exists(dst));

  Sipi::SipiImage img;
  img.read(dst);
  const Sipi::Essentials &es = img.essential_metadata();
  ASSERT_TRUE(es.is_set());

  const std::vector<std::byte> recomputed = img.compute_pixel_hash(shttps::HashType::sha256);
  EXPECT_EQ(es.fields().data_chksum, recomputed)
    << "Essentials data_chksum must match a fresh hash of the output pixels";
}

// 12.5 — `--topleft` on a 90° CW EXIF source rotates the buffer before
// stamping. The packet's `img_w` / `img_h` must reflect the rotated
// dimensions, not the source ones. `image_orientation.jpg` is 3264×2448
// with `Orientation = Rotate 90 CW` — post-rotation it is 2448×3264.
TEST(CmdConvertServiceFile, TopleftSwapsDimensionsFor90DegreeOrientation)
{
  const std::string src = materialize_fixture("unit/image_orientation.jpg", "_sfo_src_orient.jpg");
  const std::string dst = tmp_dir + "_sfo_topleft.jp2";
  ASSERT_TRUE(file_exists(src)) << "Test image not found: " << src;

  Sipi::cli::ConvertServiceFileArgs req;
  req.input_path = src;
  req.output_path = dst;
  req.set_topleft = true;

  ASSERT_EQ(Sipi::cli::cmd_convert_service_file(req), EXIT_SUCCESS);
  ASSERT_TRUE(file_exists(dst));

  const auto f = read_back_essentials(dst);
  EXPECT_EQ(f.origname, "_sfo_src_orient.jpg");
  EXPECT_EQ(f.mimetype, "image/jpeg") << "source mimetype must reflect the JPEG input";
  // Source is 3264×2448 with EXIF Orientation 6 (Rotate 90 CW). Post-rotate
  // it is 2448×3264 — the command stamps the rotated shape.
  EXPECT_EQ(f.img_w, 2448u);
  EXPECT_EQ(f.img_h, 3264u);
}

// 12.5 — Round-tripping through the command twice keys the second
// output to the first output's basename, not the upstream source. Confirms
// "Drop any Essentials packet the source happened to carry" — re-encoding
// always produces a fresh packet (per maintainer decision 2026-05-14).
TEST(CmdConvertServiceFile, DropsSourceEssentialsOnReencode)
{
  const std::string src = materialize_fixture("unit/lena512.tif", "_sfo_src_round.tif");
  const std::string intermediate = tmp_dir + "_sfo_round1.jp2";
  const std::string final_out = tmp_dir + "_sfo_round2.jp2";

  Sipi::cli::ConvertServiceFileArgs round1;
  round1.input_path = src;
  round1.output_path = intermediate;
  ASSERT_EQ(Sipi::cli::cmd_convert_service_file(round1), EXIT_SUCCESS);

  Sipi::cli::ConvertServiceFileArgs round2;
  round2.input_path = intermediate;
  round2.output_path = final_out;
  ASSERT_EQ(Sipi::cli::cmd_convert_service_file(round2), EXIT_SUCCESS);

  const auto f = read_back_essentials(final_out);
  EXPECT_EQ(f.origname, "_sfo_round1.jp2")
    << "Second pass must re-key origname to its own input, not the upstream source";
  // sipi writes `.jp2` outputs via the `jpx` SipiIO handler, so the file
  // is JPX-encoded; libmagic identifies that as "image/jpx".
  EXPECT_EQ(f.mimetype, "image/jpx");
}

// 12.5 — Output extension validation rejects anything outside the Service
// File format set (JP2 + pyramidal TIFF per ADR-0009). The command
// must exit non-zero before touching the source file.
TEST(CmdConvertServiceFile, RejectsUnsupportedOutputExtension)
{
  const std::string src = materialize_fixture("unit/lena512.tif", "_sfo_src_reject.tif");
  const std::string dst = tmp_dir + "_sfo_rejected.jpg";

  Sipi::cli::ConvertServiceFileArgs req;
  req.input_path = src;
  req.output_path = dst;

  EXPECT_EQ(Sipi::cli::cmd_convert_service_file(req), EXIT_FAILURE);
  EXPECT_FALSE(file_exists(dst)) << "Command must not create output on rejection";
}

// 12.5 — Missing input surfaces via SipiImage::readSource's exception path.
// The command catches it and exits non-zero.
TEST(CmdConvertServiceFile, FailsOnMissingInput)
{
  const std::string src = tmp_dir + "_sfo_nonexistent_input.tif";
  const std::string dst = tmp_dir + "_sfo_missing.jp2";

  Sipi::cli::ConvertServiceFileArgs req;
  req.input_path = src;
  req.output_path = dst;

  EXPECT_EQ(Sipi::cli::cmd_convert_service_file(req), EXIT_FAILURE);
  EXPECT_FALSE(file_exists(dst));
}
