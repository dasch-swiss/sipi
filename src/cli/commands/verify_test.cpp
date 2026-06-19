/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

// Verify subcommand unit tests (DEV-6537 Phase 12.7).
//
// Three command-level verify modes (the 4th `preservation-file` variant
// is a CLI-only stub that errors with "awaits ADR-0012" — tested at the
// CLI subprocess layer in Phase 12.8):
//
//   * `Generic`     — decoder-coverage only (RDU sanity). Corrupted →
//                      non-zero. Valid → 0.
//   * `AccessFile`  — Generic + assert NO Essentials packet (misclassification
//                      tripwire per ADR-0009).
//   * `ServiceFile` — Generic + assert Essentials packet present + shape
//                      consistent + pixel-hash matches `data_chksum`.
//                      Hash mismatch → non-zero + ERROR log.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ios>
#include <string>
#include <sys/stat.h>

#include "SipiImage.h"
#include "cli/commands/convert_service_file.h"
#include "cli/commands/verify.h"
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

std::string materialize_fixture(const std::string &fixture_relpath, const std::string &dst_name)
{
  const std::string src = test_images + fixture_relpath;
  const std::string dst = tmp_dir + dst_name;
  std::ifstream in(src, std::ios::binary);
  std::ofstream out(dst, std::ios::binary | std::ios::trunc);
  out << in.rdbuf();
  return dst;
}

std::string make_service_file(const std::string &fixture_relpath, const std::string &intermediate_name)
{
  const std::string src = materialize_fixture(fixture_relpath, intermediate_name + ".src");
  const std::string service_file = tmp_dir + intermediate_name + ".jp2";
  Sipi::cli::ConvertServiceFileArgs req;
  req.input_path = src;
  req.output_path = service_file;
  EXPECT_EQ(Sipi::cli::cmd_convert_service_file(req), EXIT_SUCCESS)
    << "Service File setup failed; downstream verify tests cannot run";
  return service_file;
}

}// namespace

// 12.7 — Generic verify: valid image passes. No metadata assertions in
// this mode; only the decoder needs to open + read the file.
TEST(CmdVerify, GenericPassesOnValidImage)
{
  const std::string src = materialize_fixture("unit/lena512.tif", "_vfy_generic_ok.tif");

  Sipi::cli::VerifyArgs req;
  req.mode = Sipi::cli::VerifyMode::Generic;
  req.input_path = src;

  EXPECT_EQ(Sipi::cli::cmd_verify(req), EXIT_SUCCESS);
}

// 12.7 — Generic verify: missing file fails. SipiImage::readSource throws
// SipiImageError, which the command catches and surfaces as a
// non-zero exit.
TEST(CmdVerify, GenericFailsOnMissingFile)
{
  Sipi::cli::VerifyArgs req;
  req.mode = Sipi::cli::VerifyMode::Generic;
  req.input_path = tmp_dir + "_vfy_nonexistent.tif";

  EXPECT_EQ(Sipi::cli::cmd_verify(req), EXIT_FAILURE);
}

// 12.7 — Generic verify: a corrupted JPEG fails. Truncating the file to a
// few bytes (smaller than the SOI marker block) breaks libjpeg's header
// parse — the readSource throws and verify exits non-zero.
TEST(CmdVerify, GenericFailsOnCorruptedJpeg)
{
  const std::string corrupted = tmp_dir + "_vfy_corrupted.jpg";
  std::ofstream out(corrupted, std::ios::binary);
  // 4 bytes of garbage — not a valid JPEG (no SOI 0xFFD8).
  out.write("\x00\x01\x02\x03", 4);
  out.close();
  ASSERT_TRUE(file_exists(corrupted));

  Sipi::cli::VerifyArgs req;
  req.mode = Sipi::cli::VerifyMode::Generic;
  req.input_path = corrupted;

  EXPECT_EQ(Sipi::cli::cmd_verify(req), EXIT_FAILURE);
}

// 12.7 — `verify access-file`: an Access File (no Essentials) passes. The
// JPG output from the access-file command (Phase 12.6) is the
// canonical happy-path Access File.
TEST(CmdVerify, AccessFilePassesOnImageWithoutEssentials)
{
  const std::string src = materialize_fixture("unit/lena512.tif", "_vfy_af_pass.tif");

  Sipi::cli::VerifyArgs req;
  req.mode = Sipi::cli::VerifyMode::AccessFile;
  req.input_path = src;

  EXPECT_EQ(Sipi::cli::cmd_verify(req), EXIT_SUCCESS);
}

// 12.7 — `verify access-file`: rejects a Service File. Misclassification
// tripwire — an Access File path was handed a file that carries the
// Essentials packet (which only Service Files do per ADR-0009). The
// operator likely confused `verify access-file` with `verify service-file`;
// the error message points them at the correct verb.
TEST(CmdVerify, AccessFileRejectsServiceFile)
{
  const std::string service_file = make_service_file("unit/lena512.tif", "_vfy_af_reject");

  Sipi::cli::VerifyArgs req;
  req.mode = Sipi::cli::VerifyMode::AccessFile;
  req.input_path = service_file;

  EXPECT_EQ(Sipi::cli::cmd_verify(req), EXIT_FAILURE);
}

// 12.7 — `verify service-file`: known-good Service File passes. The
// command validates Essentials presence + shape consistency + pixel
// hash matches `data_chksum` — all three are stamped by the service-file
// command, so a fresh Service File round-trips cleanly.
TEST(CmdVerify, ServiceFilePassesOnKnownGood)
{
  const std::string service_file = make_service_file("unit/lena512.tif", "_vfy_sf_good");

  Sipi::cli::VerifyArgs req;
  req.mode = Sipi::cli::VerifyMode::ServiceFile;
  req.input_path = service_file;

  EXPECT_EQ(Sipi::cli::cmd_verify(req), EXIT_SUCCESS);
}

// 12.7 — `verify service-file`: rejects a file with no Essentials. Plain
// TIFF has no SIPI carrier; the command surfaces that as a clear
// "Service Files MUST carry the packet" error.
TEST(CmdVerify, ServiceFileRejectsImageWithoutEssentials)
{
  const std::string src = materialize_fixture("unit/lena512.tif", "_vfy_sf_no_pkt.tif");

  Sipi::cli::VerifyArgs req;
  req.mode = Sipi::cli::VerifyMode::ServiceFile;
  req.input_path = src;

  EXPECT_EQ(Sipi::cli::cmd_verify(req), EXIT_FAILURE);
}

// 12.7 — `verify service-file`: rejects non-Service-File extensions. A
// .jpg input cannot be a Service File per ADR-0009 (Service Files live
// in JP2 or pyramidal TIFF carriers); the command catches this at
// the extension check, before any decode.
TEST(CmdVerify, ServiceFileRejectsNonServiceFileExtension)
{
  const std::string src = materialize_fixture("unit/MaoriFigure.jpg", "_vfy_sf_jpg.jpg");

  Sipi::cli::VerifyArgs req;
  req.mode = Sipi::cli::VerifyMode::ServiceFile;
  req.input_path = src;

  EXPECT_EQ(Sipi::cli::cmd_verify(req), EXIT_FAILURE);
}

// 12.7 — `verify service-file`: tampered pixel data triggers the pixel-hash
// corruption tripwire. Patch a single byte deep inside the JP2 codestream
// so libkakadu still decodes it (no header damage) but the resulting
// pixels differ from what `data_chksum` recorded. Verify must exit
// non-zero and surface the mismatch.
TEST(CmdVerify, ServiceFileDetectsCorruptedPixels)
{
  const std::string service_file = make_service_file("unit/lena512.tif", "_vfy_sf_corrupt");
  ASSERT_TRUE(file_exists(service_file));

  // Bit-flip a byte at a fixed offset late in the file. The Service File
  // is on the order of tens of KB; offset 4096 lands well inside the JP2
  // codestream tile data, away from the SIPI UUID box (which sits in the
  // first 64 KB prefix — see ADR-0004). Flipping one bit of one byte
  // doesn't corrupt enough to break Kakadu's decode path, but it does
  // perturb at least one decoded pixel — which is all the SHA-256 needs
  // to diverge from the stamped `data_chksum`.
  std::fstream f(service_file, std::ios::binary | std::ios::in | std::ios::out);
  ASSERT_TRUE(f.is_open());
  f.seekg(0, std::ios::end);
  const std::streamsize size = f.tellg();
  ASSERT_GT(size, 5000) << "Service File unexpectedly small; tamper offset may be invalid";
  // Tamper near the end where header/UUID-box bytes are definitely behind us.
  const std::streampos tamper_offset = size - 64;
  f.seekg(tamper_offset);
  char b = 0;
  f.read(&b, 1);
  b = static_cast<char>(b ^ 0x55);
  f.seekp(tamper_offset);
  f.write(&b, 1);
  f.close();

  Sipi::cli::VerifyArgs req;
  req.mode = Sipi::cli::VerifyMode::ServiceFile;
  req.input_path = service_file;

  EXPECT_EQ(Sipi::cli::cmd_verify(req), EXIT_FAILURE);
}
