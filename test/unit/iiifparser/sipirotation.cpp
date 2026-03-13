#include "gtest/gtest.h"

#include "iiifparser/SipiRotation.h"

struct RotationTestCase {
    std::string input;
    float expected_angle;
    bool expected_mirror;
};

class SipiRotationTest : public testing::TestWithParam<RotationTestCase> {};

TEST_P(SipiRotationTest, ParsesCorrectly) {
    const auto& tc = GetParam();
    Sipi::SipiRotation rot(tc.input);
    float angle = 0.0f;
    bool is_mirror = rot.get_rotation(angle);
    EXPECT_FLOAT_EQ(angle, tc.expected_angle);
    EXPECT_EQ(is_mirror, tc.expected_mirror);
}

INSTANTIATE_TEST_SUITE_P(Rotations, SipiRotationTest, testing::Values(
    RotationTestCase{"0", 0.0f, false},
    RotationTestCase{"90", 90.0f, false},
    RotationTestCase{"180", 180.0f, false},
    RotationTestCase{"270", 270.0f, false},
    RotationTestCase{"359.9", 359.9f, false},
    RotationTestCase{"22.5", 22.5f, false},
    RotationTestCase{"!0", 0.0f, true},
    RotationTestCase{"!90", 90.0f, true},
    RotationTestCase{"!180", 180.0f, true},
    RotationTestCase{"!22.5", 22.5f, true}
));

TEST(SipiRotation, EmptyStringDefaultsToZero) {
    Sipi::SipiRotation rot("");
    float angle = -1.0f;
    bool is_mirror = rot.get_rotation(angle);
    EXPECT_FLOAT_EQ(angle, 0.0f);
    EXPECT_FALSE(is_mirror);
}

TEST(SipiRotation, DefaultConstructor) {
    Sipi::SipiRotation rot;
    float angle = -1.0f;
    bool is_mirror = rot.get_rotation(angle);
    EXPECT_FLOAT_EQ(angle, 0.0f);
    EXPECT_FALSE(is_mirror);
}
