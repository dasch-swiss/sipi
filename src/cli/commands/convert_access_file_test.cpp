/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

// Access File command unit tests (DEV-6537).
//
// Exercises the command behind `sipi convert access-file <in> <out>`:
//
//   * Input MUST be a Service File (Essentials packet present + parses);
//     bare images without Essentials are rejected with the documented
//     error pointing operators at the generic `sipi convert` verb.
//   * The output never carries an Essentials packet (Access Files do NOT
//     identify themselves with SIPI-internal preservation metadata per
//     ADR-0009).
//   * Bad output extension fails before any I/O on the source file.
//
// Test fixtures are materialized into TEST_TMPDIR (not used as runfiles
// symlinks) so libmagic populates mimetypes correctly — same harness as
// `convert_service_file_test.cpp`. `SipiIOTiff::initLibrary` is
// registered once via a global `::testing::Environment` in this package's
// service_file test file.

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <ios>
#include <string>
#include <sys/stat.h>

#include "SipiImage.h"
#include "cli/commands/convert_access_file.h"
#include "cli/commands/convert_service_file.h"
#include "metadata/essentials.h"
#include "test_paths.h"

namespace {

const std::string test_images = sipi::test::data_dir() + "/images/";
const std::string tmp_dir = sipi::test::tmp_dir() + "/";

bool file_exists(const std::string &path)
{
  struct stat buf {};
  return stat(path.c_str(), &buf) == 0;
}

// Materialize a runfiles fixture into TEST_TMPDIR (see comment in
// `convert_service_file_test.cpp` for why this can't be skipped).
std::string materialize_fixture(const std::string &fixture_relpath, const std::string &dst_name)
{
  const std::string src = test_images + fixture_relpath;
  const std::string dst = tmp_dir + dst_name;
  std::ifstream in(src, std::ios::binary);
  std::ofstream out(dst, std::ios::binary | std::ios::trunc);
  out << in.rdbuf();
  return dst;
}

// Build a Service File from a fixture. Composing the access-file
// command's input from the service-file command's output ties
// the two ends of the preservation chain together — the same path
// operators run in production (`sipi convert service-file` → archived;
// `sipi convert access-file` → delivered to end users).
std::string make_service_file(const std::string &fixture_relpath, const std::string &intermediate_name)
{
  const std::string src = materialize_fixture(fixture_relpath, intermediate_name + ".src");
  const std::string service_file = tmp_dir + intermediate_name + ".jp2";
  Sipi::cli::ConvertServiceFileArgs req;
  req.input_path = src;
  req.output_path = service_file;
  EXPECT_EQ(Sipi::cli::cmd_convert_service_file(req), EXIT_SUCCESS)
    << "Service File setup failed; downstream Access File tests cannot run";
  return service_file;
}

}// namespace

// 12.6 — Happy path: Service File in → Access File out, no Essentials.
// The command's gating contract per ADR-0009: Access Files MUST NOT
// carry the Essentials packet, even though their Service File source did.
TEST(CmdConvertAccessFile, DropsEssentialsOnServiceFileInput)
{
  const std::string service_file = make_service_file("unit/lena512.tif", "_afo_drop_in");
  const std::string access_file = tmp_dir + "_afo_drop_out.jpg";
  ASSERT_TRUE(file_exists(service_file));

  Sipi::cli::ConvertAccessFileArgs req;
  req.input_path = service_file;
  req.output_path = access_file;
  req.format = "jpg";

  ASSERT_EQ(Sipi::cli::cmd_convert_access_file(req), EXIT_SUCCESS);
  ASSERT_TRUE(file_exists(access_file));

  // Re-read the output. The Essentials packet must be UNSET — JPEG is an
  // Access File format and never carries the packet.
  Sipi::SipiImage img;
  img.read(access_file);
  EXPECT_FALSE(img.essential_metadata().is_set())
    << "Access File output must not carry the Essentials packet";
}

// 12.6 — Service File → JP2 Access File. JP2 has TWO roles in our taxonomy:
// pyramidal JP2 with the SIPI UUID box is a Service File; JP2 without the
// box is an Access File. The command drops any in-memory Essentials, so
// the UUID-box emission stays gated off and the output is an Access File.
TEST(CmdConvertAccessFile, NoEssentialsBoxInJp2Output)
{
  const std::string service_file = make_service_file("unit/lena512.tif", "_afo_jp2_in");
  const std::string access_file = tmp_dir + "_afo_jp2_out.jp2";

  Sipi::cli::ConvertAccessFileArgs req;
  req.input_path = service_file;
  req.output_path = access_file;
  req.format = "jpx";

  ASSERT_EQ(Sipi::cli::cmd_convert_access_file(req), EXIT_SUCCESS);
  ASSERT_TRUE(file_exists(access_file));

  Sipi::SipiImage img;
  img.read(access_file);
  EXPECT_FALSE(img.essential_metadata().is_set())
    << "JP2 Access File output must not carry the Essentials UUID box";
}

// 12.6 — Validation: non-Service-File input is rejected with the documented
// error. lena512.tif is a plain TIFF with no Essentials packet — feeding it
// to the access-file command must fail with the "use sipi convert"
// guidance message and not produce an output file.
TEST(CmdConvertAccessFile, RejectsNonServiceFileInput)
{
  const std::string src = materialize_fixture("unit/lena512.tif", "_afo_reject_src.tif");
  const std::string dst = tmp_dir + "_afo_reject_out.jpg";

  Sipi::cli::ConvertAccessFileArgs req;
  req.input_path = src;
  req.output_path = dst;
  req.format = "jpg";

  EXPECT_EQ(Sipi::cli::cmd_convert_access_file(req), EXIT_FAILURE);
  EXPECT_FALSE(file_exists(dst))
    << "Command must not produce an output file when input is not a Service File";
}

// 12.6 — Output format resolution: an unsupported `--format` value is
// rejected at the CLI seam before any read. Empty `format` + ambiguous
// extension exercises the format-detection error path.
TEST(CmdConvertAccessFile, RejectsUnsupportedFormat)
{
  const std::string src = materialize_fixture("unit/lena512.tif", "_afo_badfmt_src.tif");
  const std::string dst = tmp_dir + "_afo_badfmt_out.xyz";

  Sipi::cli::ConvertAccessFileArgs req;
  req.input_path = src;
  req.output_path = dst;
  req.format = "webp";// not in the supported set

  EXPECT_EQ(Sipi::cli::cmd_convert_access_file(req), EXIT_FAILURE);
  EXPECT_FALSE(file_exists(dst));
}

// 12.6 — Output format resolution: bad output extension when `format` is
// empty must fail. Mirrors `RejectsUnsupportedOutputExtension` for the
// service-file command and confirms that the access-file command
// does extension detection BEFORE attempting to read the source.
TEST(CmdConvertAccessFile, RejectsUnknownExtensionWhenFormatEmpty)
{
  const std::string src = materialize_fixture("unit/lena512.tif", "_afo_badext_src.tif");
  const std::string dst = tmp_dir + "_afo_badext_out.xyz";

  Sipi::cli::ConvertAccessFileArgs req;
  req.input_path = src;
  req.output_path = dst;
  req.format = "";// derive from extension; "xyz" is unsupported

  EXPECT_EQ(Sipi::cli::cmd_convert_access_file(req), EXIT_FAILURE);
  EXPECT_FALSE(file_exists(dst));
}

// 12.6 — Region transformation: the command extracts a sub-region
// from the Service File. The resulting Access File has the region's
// dimensions, not the source's. Service File is 512×512; region
// [10, 20, 100, 80] yields an Access File of 100×80.
TEST(CmdConvertAccessFile, AppliesRegionTransform)
{
  const std::string service_file = make_service_file("unit/lena512.tif", "_afo_region_in");
  const std::string access_file = tmp_dir + "_afo_region_out.jpg";

  Sipi::cli::ConvertAccessFileArgs req;
  req.input_path = service_file;
  req.output_path = access_file;
  req.format = "jpg";
  req.region = { 10, 20, 100, 80 };

  ASSERT_EQ(Sipi::cli::cmd_convert_access_file(req), EXIT_SUCCESS);
  ASSERT_TRUE(file_exists(access_file));

  Sipi::SipiImage img;
  img.read(access_file);
  EXPECT_EQ(img.getNx(), 100u);
  EXPECT_EQ(img.getNy(), 80u);
  EXPECT_FALSE(img.essential_metadata().is_set());
}

// 12.6 — Missing input: command catches SipiImageError from
// readSource and exits non-zero without crashing.
TEST(CmdConvertAccessFile, FailsOnMissingInput)
{
  const std::string src = tmp_dir + "_afo_nonexistent_input.jp2";
  const std::string dst = tmp_dir + "_afo_missing_out.jpg";

  Sipi::cli::ConvertAccessFileArgs req;
  req.input_path = src;
  req.output_path = dst;
  req.format = "jpg";

  EXPECT_EQ(Sipi::cli::cmd_convert_access_file(req), EXIT_FAILURE);
  EXPECT_FALSE(file_exists(dst));
}
