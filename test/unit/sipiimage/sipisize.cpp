#include "gtest/gtest.h"

#include "../../../include/iiifparser/SipiSize.h"

TEST(SipiSize, Basic)
{
  const auto size = Sipi::SipiSize("400,300");
  EXPECT_TRUE(size.getType() == Sipi::SipiSize::PIXELS_XY);
}
