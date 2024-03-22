#include "gtest/gtest.h"

#include "handlers/iiif_handler.hpp"

using namespace handlers::iiif_handler;

TEST(iiif_handler, parse_correct_iiif_url) {
    const auto result = parse_iiif_uri("/iiif/2/image.jpg/full/200,/0/default.jpg");
    EXPECT_TRUE(result.has_value());
}

TEST(iiif_handler, parse_empty_iiif_url) {
    const auto result = parse_iiif_uri("");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "No parameters/path given");
}

TEST(iiif_handler, parse_iiif_base_uri_needing_redirect) {
    std::vector<std::tuple<std::string, IIIFUriParseResult>> valid_base_uris = {
        {"/2", IIIFUriParseResult{REDIRECT, {"", "2"}}},
        {"/iiif/3", IIIFUriParseResult{REDIRECT, {"iiif", "3"}}},
        {"/iiif/3/image1", IIIFUriParseResult{REDIRECT, {"iiif/3", "image1"}}},
        {"/iiif/3/image2", IIIFUriParseResult{REDIRECT, {"iiif/3", "image2"}}},
        {"/prefix/12345", IIIFUriParseResult{REDIRECT, {"prefix", "12345"}}},
        {"/collections/item123", IIIFUriParseResult{REDIRECT, {"collections", "item123"}}},
        {"/iiif/v2/abcd1234", IIIFUriParseResult{REDIRECT, {"iiif/v2", "abcd1234"}}},
        {"/iiif/images/5678", IIIFUriParseResult{REDIRECT, {"iiif/images", "5678"}}},
        {"/iiif/3/4/uniqueImageIdentifier", IIIFUriParseResult{REDIRECT, {"iiif/3/4", "uniqueImageIdentifier"}}},
        {"/prefix/path/to/image", IIIFUriParseResult{REDIRECT, {"prefix/path/to", "image"}}},
        {"/iiif/3/special%2Fchars%3Fhere", IIIFUriParseResult{REDIRECT, {"iiif/3", "special/chars?here"}}},
        {"/iiif/images/xyz", IIIFUriParseResult{REDIRECT, {"iiif/images", "xyz"}}},
        {"/0812/3KtDiJm4XxY-1PUUCffsF4S.jpx", IIIFUriParseResult{REDIRECT, {"0812", "3KtDiJm4XxY-1PUUCffsF4S.jpx"}}
        }
    };

    for (const auto& test_case: valid_base_uris) {
        auto result = parse_iiif_uri(std::get<0>(test_case));
        EXPECT_EQ(result, std::get<1>(test_case)) << "URI should be valid but was considered invalid: " << std::get<0>(test_case) << ", error: " << result.error() << std::endl;
    }
}

TEST(iiif_handler, not_parse_invalid_iiif_uris) {

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

    for (const auto& uri: invalid_uris) {
        auto result = parse_iiif_uri(uri);
        EXPECT_FALSE(result.has_value()) << "URI should be invalid but was considered valid: " << uri << ", parse_result: " << result->to_string() << std::endl;
    }
}
