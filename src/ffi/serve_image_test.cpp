/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Co-located unit tests (ADR-0003) for the transport-pure IIIF response builder,
// build_image_response. Because it takes only (request, engine context, cancel
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

// Collects a reported SipiImageErrorReport's fields, copying the strings —
// the struct's fields are only valid for the report_error call's duration.
struct ReportedError
{
  bool fired = false;
  std::string phase;
  std::string message;
  std::string input_file;
};

void collect_report(void *ctx, const SipiImageErrorReport *err)
{
  auto *out = static_cast<ReportedError *>(ctx);
  out->fired = true;
  out->phase = err->phase != nullptr ? err->phase : "";
  out->message = err->message != nullptr ? err->message : "";
  out->input_file = err->input_file != nullptr ? err->input_file : "";
}

}// namespace

TEST(BuildImageResponse, MissingFileIsNotFound)
{
  const std::string path = kImagesDir + "/unit/does_not_exist.tif";
  const auto params = full_params(SIPI_FORMAT_TIF);
  const auto req = make_request(path, params);

  const auto result = build_image_response(req, bare_engine(), kNeverCancelled);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), SipiStatus::NotFound);
}

TEST(BuildImageResponse, DirectPassthroughIsFileBody)
{
  // Same format as the source (TIFF), full region + size, no transform → the
  // engine serves the file directly (sendFile), not a re-encode.
  const std::string path = fixture("/unit/lena512.tif");
  const auto params = full_params(SIPI_FORMAT_TIF);
  const auto req = make_request(path, params);

  const auto result = build_image_response(req, bare_engine(), kNeverCancelled);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->http_status, 200);
  EXPECT_TRUE(std::holds_alternative<FileBody>(result->body));
  EXPECT_EQ(std::get<FileBody>(result->body).path, path);
  EXPECT_TRUE(has_header(*result, "Content-Type", "image/tiff"));
}

TEST(BuildImageResponse, FormatConversionIsStreamBody)
{
  // A different output format (PNG) forces a decode + re-encode: the body is a
  // streamed producer, not a file passthrough.
  const std::string path = fixture("/unit/lena512.tif");
  const auto params = full_params(SIPI_FORMAT_PNG);
  const auto req = make_request(path, params);

  const auto result = build_image_response(req, bare_engine(), kNeverCancelled);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->http_status, 200);
  EXPECT_TRUE(std::holds_alternative<StreamBody>(result->body));
  EXPECT_TRUE(has_header(*result, "Content-Type", "image/png"));
}

TEST(BuildImageResponse, HeadRequestIsEmptyBody)
{
  const std::string path = fixture("/unit/lena512.tif");
  const auto params = full_params(SIPI_FORMAT_PNG);
  const auto req = make_request(path, params, /*is_head=*/1);

  const auto result = build_image_response(req, bare_engine(), kNeverCancelled);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->http_status, 200);
  EXPECT_TRUE(std::holds_alternative<EmptyBody>(result->body));
  EXPECT_TRUE(has_header(*result, "Content-Type", "image/png"));
}

TEST(BuildImageResponse, OutputPixelLimitIsBadRequest)
{
  auto eng = bare_engine();
  eng.max_pixel_limit = 1000;// the 512x512 = 262144-pixel output blows the cap

  const std::string path = fixture("/unit/lena512.tif");
  const auto params = full_params(SIPI_FORMAT_PNG);
  const auto req = make_request(path, params);

  const auto result = build_image_response(req, eng, kNeverCancelled);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), SipiStatus::BadRequest);
}

TEST(BuildImageResponse, ClientGoneBeforeDecode)
{
  const std::string path = fixture("/unit/lena512.tif");
  const auto params = full_params(SIPI_FORMAT_PNG);// decode path (polls cancel)
  const auto req = make_request(path, params);

  const auto always_cancelled = std::function<bool()>([] { return true; });
  const auto result = build_image_response(req, bare_engine(), always_cancelled);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), SipiStatus::ClientGone);
}

TEST(BuildImageResponse, ReportErrorFiresOnReadFailure)
{
  // test.csv exists but is not a recognised image format — read_shape()
  // throws before any decode is attempted, exercising the seam's earliest
  // report_error call site.
  const std::string path = fixture("/unit/test.csv");
  const auto params = full_params(SIPI_FORMAT_TIF);
  auto req = make_request(path, params);
  ReportedError out;
  req.report_error = collect_report;
  req.report_ctx = &out;

  const auto result = build_image_response(req, bare_engine(), kNeverCancelled);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), SipiStatus::InternalError);
  EXPECT_TRUE(out.fired);
  EXPECT_EQ(out.phase, "read");
  EXPECT_FALSE(out.message.empty());
  EXPECT_EQ(out.input_file, path);
}

TEST(BuildImageResponse, ReportErrorNullCallbackIsSafeNoOp)
{
  // Same failing request as above, but report_error is left null (make_request's
  // SipiServeRequest{} default) — the seam's "NULL = absent" idiom must not
  // crash or otherwise change the returned status.
  const std::string path = fixture("/unit/test.csv");
  const auto params = full_params(SIPI_FORMAT_TIF);
  const auto req = make_request(path, params);

  const auto result = build_image_response(req, bare_engine(), kNeverCancelled);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), SipiStatus::InternalError);
}
