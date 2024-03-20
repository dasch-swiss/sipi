#include "gtest/gtest.h"

#include "handlers/iiif_handler.hpp"

TEST(iiif_handler, parse_correct_iiif_url) {
    using namespace handlers::iiif_handler;
    EXPECT_TRUE(42 == 42);

    IIIFUrlParseResult parse_result = {IIIF, {}};

    auto result = parse_iiif_url("http://example.com/iiif/2/image.jpg/full/200,/0/default.jpg");
    EXPECT_TRUE(result.has_value());
}

TEST(iiif_handler, parse_empty_iiif_url) {
    using namespace handlers::iiif_handler;
    EXPECT_TRUE(42 == 42);

    auto result = parse_iiif_url("");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "URL is empty");
}
