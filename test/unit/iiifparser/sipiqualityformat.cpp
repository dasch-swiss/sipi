#include "gtest/gtest.h"

#include "iiifparser/SipiQualityFormat.h"
#include "SipiError.hpp"

struct QualityFormatTestCase {
    std::string input;
    Sipi::SipiQualityFormat::QualityType expected_quality;
    Sipi::SipiQualityFormat::FormatType expected_format;
};

class SipiQualityFormatTest : public testing::TestWithParam<QualityFormatTestCase> {};

TEST_P(SipiQualityFormatTest, ParsesCorrectly) {
    const auto& tc = GetParam();
    Sipi::SipiQualityFormat qf(tc.input);
    EXPECT_EQ(qf.quality(), tc.expected_quality);
    EXPECT_EQ(qf.format(), tc.expected_format);
}

INSTANTIATE_TEST_SUITE_P(QualityFormats, SipiQualityFormatTest, testing::Values(
    // All quality types with jpg
    QualityFormatTestCase{"default.jpg", Sipi::SipiQualityFormat::DEFAULT, Sipi::SipiQualityFormat::JPG},
    QualityFormatTestCase{"color.jpg", Sipi::SipiQualityFormat::COLOR, Sipi::SipiQualityFormat::JPG},
    QualityFormatTestCase{"gray.jpg", Sipi::SipiQualityFormat::GRAY, Sipi::SipiQualityFormat::JPG},
    QualityFormatTestCase{"bitonal.jpg", Sipi::SipiQualityFormat::BITONAL, Sipi::SipiQualityFormat::JPG},
    // All format types with default quality
    QualityFormatTestCase{"default.tif", Sipi::SipiQualityFormat::DEFAULT, Sipi::SipiQualityFormat::TIF},
    QualityFormatTestCase{"default.png", Sipi::SipiQualityFormat::DEFAULT, Sipi::SipiQualityFormat::PNG},
    QualityFormatTestCase{"default.gif", Sipi::SipiQualityFormat::DEFAULT, Sipi::SipiQualityFormat::GIF},
    QualityFormatTestCase{"default.jp2", Sipi::SipiQualityFormat::DEFAULT, Sipi::SipiQualityFormat::JP2},
    QualityFormatTestCase{"default.pdf", Sipi::SipiQualityFormat::DEFAULT, Sipi::SipiQualityFormat::PDF},
    QualityFormatTestCase{"default.webp", Sipi::SipiQualityFormat::DEFAULT, Sipi::SipiQualityFormat::WEBP},
    // Unsupported format
    QualityFormatTestCase{"default.bmp", Sipi::SipiQualityFormat::DEFAULT, Sipi::SipiQualityFormat::UNSUPPORTED}
));

TEST(SipiQualityFormat, DefaultConstructor) {
    Sipi::SipiQualityFormat qf;
    EXPECT_EQ(qf.quality(), Sipi::SipiQualityFormat::DEFAULT);
    EXPECT_EQ(qf.format(), Sipi::SipiQualityFormat::JPG);
}

TEST(SipiQualityFormat, EmptyStringDefaultsToJpg) {
    Sipi::SipiQualityFormat qf("");
    EXPECT_EQ(qf.quality(), Sipi::SipiQualityFormat::DEFAULT);
    EXPECT_EQ(qf.format(), Sipi::SipiQualityFormat::JPG);
}

TEST(SipiQualityFormat, MissingDotThrows) {
    EXPECT_THROW(Sipi::SipiQualityFormat("defaultjpg"), Sipi::SipiError);
}

TEST(SipiQualityFormat, InvalidQualityThrows) {
    EXPECT_THROW(Sipi::SipiQualityFormat("invalid.jpg"), Sipi::SipiError);
}
