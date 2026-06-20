/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Co-located unit tests (ADR-0003) for the transport-pure IIIF decision,
// decide_serve_image. Because it takes only (request, engine context, cancel
// predicate) it runs without a socket: the body shape (FileBody / StreamBody /
// EmptyBody) and the pre-commit status codes are checked directly.

#include "gtest/gtest.h"

#include <climits>
#include <cstdlib>
#include <functional>
#include <string>
#include <variant>

#include "ffi/engine_context.h"
#include "ffi/serve_image.h"
#include "ffi/serve_response.h"
#include "ffi/sipi_ffi.h"
#include "test_paths.h"

namespace {

using namespace Sipi::ffi;

const std::string kImagesDir = sipi::test::data_dir() + "/images";

// Resolve the fixture through realpath(), matching what the real serve path
// hands the FFI (R2 path validation realpath()s every request). It also sidesteps
// a Bazel-runfiles artifact: the fixture is a symlink, and libmagic would sniff
// the *symlink* (inode/symlink) rather than the TIFF behind it.
std::string fixture(const std::string &rel)
{
  const std::string p = kImagesDir + rel;
  char buf[PATH_MAX];
  return realpath(p.c_str(), buf) != nullptr ? std::string(buf) : p;
}

const auto kNeverCancelled = std::function<bool()>([] { return false; });

// A FULL-region / FULL-size / no-rotation request for the given output format.
SipiIiifParams full_params(SipiFormatType format, SipiQualityType quality = SIPI_QUALITY_DEFAULT)
{
  SipiIiifParams p{};
  p.region_type = SIPI_REGION_FULL;
  p.size_type = SIPI_SIZE_FULL;
  p.rotation = 0.F;
  p.rotation_mirror = 0;
  p.quality_type = quality;
  p.format_type = format;
  return p;
}

EngineContext bare_engine()
{
  EngineContext eng;// all services null (cache/rate-limit/budget disabled)
  eng.imgroot = kImagesDir;
  eng.resolved_imgroot = kImagesDir;
  eng.jpeg_quality = 60;
  return eng;
}

SipiServeRequest make_request(const std::string &path, const SipiIiifParams &params, int is_head = 0)
{
  SipiServeRequest req{};
  req.resolved_path = path.c_str();
  req.prefix = "unit";
  req.identifier = "lena512.tif";
  req.client_ip = "127.0.0.1";
  req.params = params;
  req.restricted_size = nullptr;
  req.watermark_path = nullptr;
  req.forwarded_proto = nullptr;
  req.forwarded_host = "localhost";
  req.request_uri = "/unit/lena512.tif";
  req.is_head = is_head;
  return req;
}

bool has_header(const ServeResponse &r, const std::string &name, const std::string &value)
{
  for (const auto &[n, v] : r.headers) {
    if (n == name && v == value) { return true; }
  }
  return false;
}

}// namespace

TEST(DecideServeImage, MissingFileIsNotFound)
{
  const std::string path = kImagesDir + "/unit/does_not_exist.tif";
  const auto params = full_params(SIPI_FORMAT_TIF);
  const auto req = make_request(path, params);

  const auto decision = decide_serve_image(req, bare_engine(), kNeverCancelled);
  ASSERT_FALSE(decision.has_value());
  EXPECT_EQ(decision.error(), SipiStatus::NotFound);
}

TEST(DecideServeImage, DirectPassthroughIsFileBody)
{
  // Same format as the source (TIFF), full region + size, no transform → the
  // engine serves the file directly (sendFile), not a re-encode.
  const std::string path = fixture("/unit/lena512.tif");
  const auto params = full_params(SIPI_FORMAT_TIF);
  const auto req = make_request(path, params);

  const auto decision = decide_serve_image(req, bare_engine(), kNeverCancelled);
  ASSERT_TRUE(decision.has_value());
  EXPECT_EQ(decision->http_status, 200);
  EXPECT_TRUE(std::holds_alternative<FileBody>(decision->body));
  EXPECT_EQ(std::get<FileBody>(decision->body).path, path);
  EXPECT_TRUE(has_header(*decision, "Content-Type", "image/tiff"));
}

TEST(DecideServeImage, FormatConversionIsStreamBody)
{
  // A different output format (PNG) forces a decode + re-encode: the body is a
  // streamed producer, not a file passthrough.
  const std::string path = fixture("/unit/lena512.tif");
  const auto params = full_params(SIPI_FORMAT_PNG);
  const auto req = make_request(path, params);

  const auto decision = decide_serve_image(req, bare_engine(), kNeverCancelled);
  ASSERT_TRUE(decision.has_value());
  EXPECT_EQ(decision->http_status, 200);
  EXPECT_TRUE(std::holds_alternative<StreamBody>(decision->body));
  EXPECT_TRUE(has_header(*decision, "Content-Type", "image/png"));
}

TEST(DecideServeImage, HeadRequestIsEmptyBody)
{
  const std::string path = fixture("/unit/lena512.tif");
  const auto params = full_params(SIPI_FORMAT_PNG);
  const auto req = make_request(path, params, /*is_head=*/1);

  const auto decision = decide_serve_image(req, bare_engine(), kNeverCancelled);
  ASSERT_TRUE(decision.has_value());
  EXPECT_EQ(decision->http_status, 200);
  EXPECT_TRUE(std::holds_alternative<EmptyBody>(decision->body));
  EXPECT_TRUE(has_header(*decision, "Content-Type", "image/png"));
}

TEST(DecideServeImage, OutputPixelLimitIsBadRequest)
{
  auto eng = bare_engine();
  eng.max_pixel_limit = 1000;// the 512x512 = 262144-pixel output blows the cap

  const std::string path = fixture("/unit/lena512.tif");
  const auto params = full_params(SIPI_FORMAT_PNG);
  const auto req = make_request(path, params);

  const auto decision = decide_serve_image(req, eng, kNeverCancelled);
  ASSERT_FALSE(decision.has_value());
  EXPECT_EQ(decision.error(), SipiStatus::BadRequest);
}

TEST(DecideServeImage, ClientGoneBeforeDecode)
{
  const std::string path = fixture("/unit/lena512.tif");
  const auto params = full_params(SIPI_FORMAT_PNG);// decode path (polls cancel)
  const auto req = make_request(path, params);

  const auto always_cancelled = std::function<bool()>([] { return true; });
  const auto decision = decide_serve_image(req, bare_engine(), always_cancelled);
  ASSERT_FALSE(decision.has_value());
  EXPECT_EQ(decision.error(), SipiStatus::ClientGone);
}
