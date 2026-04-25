/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "handlers/iiif_handler.hpp"

using namespace handlers::iiif_handler;

namespace {

// Run `f` in a detached thread; return true if it completed within `timeout`.
// On timeout the worker thread is leaked; the test fails and the process exits,
// so the OS reaps the thread.
template<typename F> [[nodiscard]] bool completes_within(std::chrono::milliseconds timeout, F f)
{
  auto done = std::make_shared<std::atomic<bool>>(false);
  std::thread([done, fn = std::move(f)]() mutable {
    fn();
    done->store(true, std::memory_order_release);
  }).detach();

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (done->load(std::memory_order_acquire)) { return true; }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

constexpr auto kParseBudget = std::chrono::milliseconds(100);

}// namespace

TEST(iiif_handler, parse_correct_iiif_url)
{
  const auto result = parse_iiif_uri("/iiif/2/image.jpg/full/200,/0/default.jpg");
  EXPECT_TRUE(result.has_value());
}

TEST(iiif_handler, parse_correct_iiif_url_asserts)
{
  std::vector<std::tuple<std::string, IIIFUriParseResult>> valid_base_uris = {
    { "/iiif/2/image.jpg/full/200,/0/default.jpg",
      IIIFUriParseResult{ IIIF, { "iiif/2", "image.jpg", "full", "200,", "0", "default.jpg" } } }
  };

  for (const auto &test_case : valid_base_uris) {
    auto result = parse_iiif_uri(std::get<0>(test_case));

    std::ostringstream actual;

    if (result)
      actual << *result;
    else
      actual << result.error();

    EXPECT_EQ(result, std::get<1>(test_case)) << "for " << std::get<0>(test_case) << " expected "
                                              << std::get<1>(test_case) << " got " << actual.str() << std::endl;
  }
}

TEST(iiif_handler, parse_empty_iiif_url)
{
  const auto result = parse_iiif_uri("");
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), "No parameters/path given");
}

TEST(iiif_handler, parse_iiif_base_uri_needing_redirect)
{
  std::vector<std::tuple<std::string, IIIFUriParseResult>> valid_base_uris = {
    { "/2", IIIFUriParseResult{ REDIRECT, { "", "2" } } },
    { "/iiif/3", IIIFUriParseResult{ REDIRECT, { "iiif", "3" } } },
    { "/iiif/3/image1", IIIFUriParseResult{ REDIRECT, { "iiif/3", "image1" } } },
    { "/iiif/3/image2", IIIFUriParseResult{ REDIRECT, { "iiif/3", "image2" } } },
    { "/prefix/12345", IIIFUriParseResult{ REDIRECT, { "prefix", "12345" } } },
    { "/collections/item123", IIIFUriParseResult{ REDIRECT, { "collections", "item123" } } },
    { "/iiif/v2/abcd1234", IIIFUriParseResult{ REDIRECT, { "iiif/v2", "abcd1234" } } },
    { "/iiif/images/5678", IIIFUriParseResult{ REDIRECT, { "iiif/images", "5678" } } },
    { "/iiif/3/4/uniqueImageIdentifier", IIIFUriParseResult{ REDIRECT, { "iiif/3/4", "uniqueImageIdentifier" } } },
    { "/prefix/path/to/image", IIIFUriParseResult{ REDIRECT, { "prefix/path/to", "image" } } },
    { "/iiif/3/special%2Fchars%3Fhere", IIIFUriParseResult{ REDIRECT, { "iiif/3", "special/chars?here" } } },
    { "/iiif/images/xyz", IIIFUriParseResult{ REDIRECT, { "iiif/images", "xyz" } } },
    { "/0812/3KtDiJm4XxY-1PUUCffsF4S.jpx", IIIFUriParseResult{ REDIRECT, { "0812", "3KtDiJm4XxY-1PUUCffsF4S.jpx" } } }
  };

  for (const auto &test_case : valid_base_uris) {
    auto result = parse_iiif_uri(std::get<0>(test_case));
    EXPECT_EQ(result, std::get<1>(test_case))
      << "URI should be valid but was considered invalid: " << std::get<0>(test_case) << ", error: " << result.error()
      << std::endl;
  }
}

TEST(iiif_handler, not_parse_invalid_iiif_uris)
{

  std::vector<std::string> invalid_uris = {
    "/",
    "//2/",
    "/unit//lena512.jp2",
    "/unit/lena512.jp2/max/0/default.jpg",
    "/unit/lena512.jp2/full/max/default.jpg",
    "/unit/lena512.jp2/full/max/!/default.jpg",
    "/unit/lena512.jp2/full/max/0/jpg",
    "/knora/67352ccc-d1b0-11e1-89ae-279075081939.jp2/full/max/0/default.aN",
    "/knora/67352ccc-d1b0-11e1-89ae-279075081939.jp2/full/max/0/BFTP=w.jpg",
  };

  for (const auto &uri : invalid_uris) {
    auto result = parse_iiif_uri(uri);
    EXPECT_FALSE(result.has_value()) << "URI should be invalid but was considered valid: " << uri
                                     << ", parse_result: " << result->to_string() << std::endl;
  }
}

// IIIF 3.0 coverage matrix — every URL form advertised by `info.json` has at
// least one corresponding parser test below.

TEST(iiif_handler, parse_region_forms)
{
  const std::vector<std::string> region_forms = {
    "full",
    "square",
    "0,0,100,100",
    "10,20,300,400",
    "pct:25.5,25.5,50.0,50.0",
    "pct:0,0,100,100",
  };
  for (const auto &region : region_forms) {
    const std::string uri = "/p/img.jp2/" + region + "/max/0/default.jpg";
    auto result = parse_iiif_uri(uri);
    EXPECT_TRUE(result.has_value()) << "region form rejected: " << region
                                    << (result.has_value() ? "" : " — error: " + result.error());
  }
}

TEST(iiif_handler, parse_size_forms_no_upscale)
{
  const std::vector<std::string> size_forms = {
    "max",
    "pct:50",
    "pct:50.5",
    "100,",
    ",100",
    "100,100",
    "!100,100",
  };
  for (const auto &size : size_forms) {
    const std::string uri = "/p/img.jp2/full/" + size + "/0/default.jpg";
    auto result = parse_iiif_uri(uri);
    EXPECT_TRUE(result.has_value()) << "size form rejected: " << size
                                    << (result.has_value() ? "" : " — error: " + result.error());
  }
}

TEST(iiif_handler, parse_size_forms_upscale)
{
  const std::vector<std::string> upscale_forms = {
    "^max",
    "^pct:150",
    "^200,",
    "^,200",
    "^200,200",
    "^!200,200",
  };
  for (const auto &size : upscale_forms) {
    const std::string uri = "/p/img.jp2/full/" + size + "/0/default.jpg";
    auto result = parse_iiif_uri(uri);
    EXPECT_TRUE(result.has_value()) << "upscale size form rejected: " << size
                                    << (result.has_value() ? "" : " — error: " + result.error());
  }
}

TEST(iiif_handler, parse_rotation_forms)
{
  const std::vector<std::string> rotation_forms = {
    "0",
    "90",
    "180",
    "270",
    "45.5",
    "359.999",
    "!90",
    "!180",
    "!0.5",
  };
  for (const auto &rotation : rotation_forms) {
    const std::string uri = "/p/img.jp2/full/max/" + rotation + "/default.jpg";
    auto result = parse_iiif_uri(uri);
    EXPECT_TRUE(result.has_value()) << "rotation form rejected: " << rotation
                                    << (result.has_value() ? "" : " — error: " + result.error());
  }
}

TEST(iiif_handler, parse_quality_format_combinations)
{
  const std::vector<std::string> qualities = { "color", "gray", "bitonal", "default" };
  const std::vector<std::string> formats = { "jpg", "tif", "png", "jp2" };
  for (const auto &q : qualities) {
    for (const auto &f : formats) {
      const std::string uri = "/p/img.jp2/full/max/0/" + q + "." + f;
      auto result = parse_iiif_uri(uri);
      EXPECT_TRUE(result.has_value()) << "quality.format rejected: " << q << "." << f;
    }
  }
}

TEST(iiif_handler, reject_unsupported_formats)
{
  const std::vector<std::string> bad_formats = { "gif", "webp", "pdf", "bmp", "svg" };
  for (const auto &f : bad_formats) {
    const std::string uri = "/p/img.jp2/full/max/0/default." + f;
    auto result = parse_iiif_uri(uri);
    EXPECT_FALSE(result.has_value()) << "format must be rejected: " << f;
  }
}

TEST(iiif_handler, reject_signed_rotations)
{
  // IIIF Image API 3.0 §5.1.1: floating-point rotations are decimal digits and
  // '.' only — no leading sign.
  const std::vector<std::string> bad_rotations = { "+0", "+90", "-90", "-180.5", "+45.5" };
  for (const auto &r : bad_rotations) {
    const std::string uri = "/p/img.jp2/full/max/" + r + "/default.jpg";
    auto result = parse_iiif_uri(uri);
    EXPECT_FALSE(result.has_value()) << "signed rotation must be rejected: " << r;
  }
}

TEST(iiif_handler, reject_empty_size_fields)
{
  const std::vector<std::string> bad_sizes = { ",", "pct:", "^,", "^pct:", "!," };
  for (const auto &s : bad_sizes) {
    const std::string uri = "/p/img.jp2/full/" + s + "/0/default.jpg";
    auto result = parse_iiif_uri(uri);
    EXPECT_FALSE(result.has_value()) << "size with empty fields must be rejected: " << s;
  }
}

TEST(iiif_handler, reject_malformed_posfloat)
{
  // posfloat per IIIF 3.0 §5.1.1: digit+ ('.' digit+)?
  const std::vector<std::string> bad_rotations = { ".5", "0.", ".", "1..2", "abc" };
  for (const auto &r : bad_rotations) {
    const std::string uri = "/p/img.jp2/full/max/" + r + "/default.jpg";
    auto result = parse_iiif_uri(uri);
    EXPECT_FALSE(result.has_value()) << "malformed posfloat rotation must be rejected: " << r;
  }
}

TEST(iiif_handler, reject_malformed_region)
{
  const std::vector<std::string> bad_regions = {
    "1,2,3",          // too few
    "1,2,3,4,5",      // too many
    "pct:1,2,3",      // pct: too few
    "pct:1,2,3,4,5",  // pct: too many
    "abc",            // not a keyword
    "pct:",           // empty
    ",,,",            // empty fields
  };
  for (const auto &r : bad_regions) {
    const std::string uri = "/p/img.jp2/" + r + "/max/0/default.jpg";
    auto result = parse_iiif_uri(uri);
    EXPECT_FALSE(result.has_value()) << "malformed region must be rejected: " << r;
  }
}

TEST(iiif_handler, parse_info_json)
{
  auto result = parse_iiif_uri("/p/img.jp2/info.json");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->request_type, INFO_JSON);
  EXPECT_EQ(result->params, (std::vector<std::string>{ "p", "img.jp2" }));
}

TEST(iiif_handler, parse_knora_json)
{
  auto result = parse_iiif_uri("/p/img.jp2/knora.json");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->request_type, KNORA_JSON);
  EXPECT_EQ(result->params, (std::vector<std::string>{ "p", "img.jp2" }));
}

TEST(iiif_handler, parse_file_download)
{
  auto result = parse_iiif_uri("/p/img.jp2/file");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->request_type, FILE_DOWNLOAD);
  EXPECT_EQ(result->params, (std::vector<std::string>{ "p", "img.jp2" }));
}

// Inputs containing a `/`-separated component ending in `%`. The assertion is
// "parse returns within the wall-clock budget" — the return value is irrelevant.

TEST(iiif_handler, no_hang_on_segment_ending_in_percent_a)
{
  const std::string input("/uniunith%/]a0s6.\x00\x61%0s6.\x00\x1b;sta%0s6", 34);
  EXPECT_TRUE(completes_within(kParseBudget, [&] { (void)parse_iiif_uri(input); }));
}

TEST(iiif_handler, no_hang_on_segment_ending_in_run_of_percents)
{
  const std::string input("///fla///////////////////c/\xf4%%%%%%%%%%%%%%%%%%%", 47);
  EXPECT_TRUE(completes_within(kParseBudget, [&] { (void)parse_iiif_uri(input); }));
}

TEST(iiif_handler, no_hang_on_segment_ending_in_percent_b)
{
  const std::string input("/unit/lena512.jp2/pct:30,10\x1a\x00\x00\x00\x00\x00\x00\x00/unit/lena%", 46);
  EXPECT_TRUE(completes_within(kParseBudget, [&] { (void)parse_iiif_uri(input); }));
}

TEST(iiif_handler, no_hang_on_short_segment_ending_in_percent)
{
  const std::string input("/a\x80\xff\xff\xff\xff\xff\xff\xff/Gb%", 14);
  EXPECT_TRUE(completes_within(kParseBudget, [&] { (void)parse_iiif_uri(input); }));
}
