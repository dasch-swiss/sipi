#include "gtest/gtest.h"

#include "iiifparser/SipiIdentifier.h"

TEST(SipiIdentifier, SimpleIdentifier) {
    Sipi::SipiIdentifier id("test_image.jp2");
    EXPECT_EQ(id.getIdentifier(), "test_image.jp2");
    EXPECT_EQ(id.getPage(), 0);
}

TEST(SipiIdentifier, IdentifierWithPage) {
    Sipi::SipiIdentifier id("test_image.jp2@3");
    EXPECT_EQ(id.getIdentifier(), "test_image.jp2");
    EXPECT_EQ(id.getPage(), 3);
}

TEST(SipiIdentifier, IdentifierWithPageZero) {
    Sipi::SipiIdentifier id("image.tif@0");
    EXPECT_EQ(id.getIdentifier(), "image.tif");
    EXPECT_EQ(id.getPage(), 0);
}

TEST(SipiIdentifier, UrlEncodedIdentifier) {
    // %2F is URL-encoded forward slash
    Sipi::SipiIdentifier id("path%2Fto%2Fimage.jp2");
    EXPECT_EQ(id.getIdentifier(), "path/to/image.jp2");
    EXPECT_EQ(id.getPage(), 0);
}

TEST(SipiIdentifier, UrlEncodedWithPage) {
    Sipi::SipiIdentifier id("path%2Fimage.jp2@5");
    EXPECT_EQ(id.getIdentifier(), "path/image.jp2");
    EXPECT_EQ(id.getPage(), 5);
}

TEST(SipiIdentifier, InvalidPageFallsBackToZero) {
    Sipi::SipiIdentifier id("image.jp2@notanumber");
    EXPECT_EQ(id.getIdentifier(), "image.jp2");
    EXPECT_EQ(id.getPage(), 0);
}

TEST(SipiIdentifier, DefaultConstructor) {
    Sipi::SipiIdentifier id;
    EXPECT_EQ(id.getIdentifier(), "");
    EXPECT_EQ(id.getPage(), 0);
}
