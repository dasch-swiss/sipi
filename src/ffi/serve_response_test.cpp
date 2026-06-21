/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gtest/gtest.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

#include "ffi/serve_response.h"

namespace {

namespace fs = std::filesystem;
using Sipi::ffi::build_file_response;
using Sipi::ffi::EmptyBody;
using Sipi::ffi::FileBody;
using Sipi::ffi::ServeResponse;
using Sipi::ffi::SipiStatus;

fs::path make_temp_file(const std::string &name, const std::string &content)
{
  const char *dir = std::getenv("TEST_TMPDIR");
  const fs::path base = (dir != nullptr) ? fs::path(dir) : fs::temp_directory_path();
  const fs::path path = base / name;
  std::ofstream(path, std::ios::binary) << content;
  return path;
}

std::string header_value(const ServeResponse &r, const std::string &name)
{
  for (const auto &[key, value] : r.headers) {
    if (key == name) { return value; }
  }
  return {};
}

}// namespace

TEST(BuildFileResponse, FullFileIsContentLengthFileBody)
{
  const auto path = make_temp_file("serve_full.bin", std::string(1000, 'x'));

  const auto result = build_file_response(path.c_str(), nullptr);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->http_status, 200);
  const auto *file = std::get_if<FileBody>(&result->body);
  ASSERT_NE(file, nullptr);
  EXPECT_EQ(file->path, path.string());
  EXPECT_EQ(file->offset, 0u);
  EXPECT_EQ(file->length, 1000u);
  EXPECT_FALSE(header_value(*result, "Content-Type").empty());
  EXPECT_EQ(header_value(*result, "Accept-Ranges"), "bytes");
  EXPECT_TRUE(header_value(*result, "Content-Range").empty());
}

TEST(BuildFileResponse, RangePrefix)
{
  const auto path = make_temp_file("serve_range.bin", std::string(1000, 'x'));

  const auto result = build_file_response(path.c_str(), "bytes=0-99");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->http_status, 206);
  const auto *file = std::get_if<FileBody>(&result->body);
  ASSERT_NE(file, nullptr);
  EXPECT_EQ(file->offset, 0u);
  EXPECT_EQ(file->length, 100u);
  EXPECT_EQ(header_value(*result, "Content-Range"), "bytes 0-99/1000");
}

TEST(BuildFileResponse, RangeOpenEnded)
{
  const auto path = make_temp_file("serve_open.bin", std::string(1000, 'x'));

  const auto result = build_file_response(path.c_str(), "bytes=500-");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->http_status, 206);
  const auto *file = std::get_if<FileBody>(&result->body);
  ASSERT_NE(file, nullptr);
  EXPECT_EQ(file->offset, 500u);
  EXPECT_EQ(file->length, 500u);
  EXPECT_EQ(header_value(*result, "Content-Range"), "bytes 500-999/1000");
}

// The clamp: a Range whose end exceeds EOF must yield a Content-Range
// last-byte-pos equal to the delivered length, never the (larger) request.
TEST(BuildFileResponse, RangeEndBeyondEofIsClamped)
{
  const auto path = make_temp_file("serve_clamp.bin", std::string(10, 'x'));

  const auto result = build_file_response(path.c_str(), "bytes=0-999999");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->http_status, 206);
  const auto *file = std::get_if<FileBody>(&result->body);
  ASSERT_NE(file, nullptr);
  EXPECT_EQ(file->length, 10u);
  EXPECT_EQ(header_value(*result, "Content-Range"), "bytes 0-9/10");
}

TEST(BuildFileResponse, NonexistentIsNotFound)
{
  const auto result = build_file_response("/no/such/file/sipi-ffi-xyz123", nullptr);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(static_cast<int>(result.error()), 404);
}

TEST(BuildFileResponse, MalformedRangeIsBadRequest)
{
  const auto path = make_temp_file("serve_bad.bin", std::string(100, 'x'));

  const auto result = build_file_response(path.c_str(), "bytes=abc");

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(static_cast<int>(result.error()), 400);
}

TEST(BuildFileResponse, StartBeyondEofIsError)
{
  const auto path = make_temp_file("serve_beyond.bin", std::string(10, 'x'));

  const auto result = build_file_response(path.c_str(), "bytes=100-200");

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(static_cast<int>(result.error()), 500);
}

TEST(BuildFileResponse, EmptyFileIsEmptyBody)
{
  const auto path = make_temp_file("serve_empty.bin", "");

  const auto result = build_file_response(path.c_str(), nullptr);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->http_status, 200);
  EXPECT_TRUE(std::holds_alternative<EmptyBody>(result->body));
}
