#include "gtest/gtest.h"

#include "handlers/iiif_handler.hpp"

using namespace handlers::iiif_handler;

TEST(iiif_handler, parse_correct_iiif_url) {
    const auto result = parse_iiif_url("http://example.com/iiif/2/image.jpg/full/200,/0/default.jpg");
    EXPECT_TRUE(result.has_value());
}

TEST(iiif_handler, parse_empty_iiif_url) {
    const auto result = parse_iiif_url("");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "No parameters/path given");
}

TEST(iiif_handler, parse_iiif_url_needing_redirect) {
    const auto result = parse_iiif_url("https://iiif.dasch.swiss/0812/3KtDiJm4XxY-1PUUCffsF4S.jpx");
    EXPECT_TRUE(result.has_value());
    std::cout << result.value().to_string() << std::endl;
    EXPECT_EQ(result->request_type, REDIRECT);
}

TEST(iiif_handler, not_parse_incomplete_iiif_url) {
    const auto result = parse_iiif_url("https://iiif.dasch.swiss/0812");
    EXPECT_FALSE(result.has_value());
    std::cout << result.value().to_string() << std::endl;
    EXPECT_EQ(result.error(), "No parameters/path given");
}
