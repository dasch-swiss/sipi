#include "gtest/gtest.h"

#include "../../../include/iiifparser/SipiSize.h"

TEST(SipiSize, PixelsXY)
{
  auto size = Sipi::SipiSize("400,300");
  EXPECT_TRUE(size.getType() == Sipi::SipiSize::PIXELS_XY);

  size_t w, h;
  int reduce;
  bool reduce_only;

  size.get_size(400, 300, w, h, reduce, reduce_only);
  EXPECT_TRUE(w == 400 && h == 300 && reduce == 0 && reduce_only == 1);
}

TEST(SipiSize, Percent)
{
  auto size = Sipi::SipiSize("pct:25");
  EXPECT_TRUE(size.getType() == Sipi::SipiSize::PERCENTS);

  size_t w, h;
  int reduce;
  bool reduce_only;

  size.get_size(400, 300, w, h, reduce, reduce_only);
  EXPECT_TRUE(w == 100 && h == 75 && reduce == 2 && reduce_only == 1);
}

TEST(SipiSize, Maxdim)
{
  auto size = Sipi::SipiSize("!200,200");
  EXPECT_TRUE(size.getType() == Sipi::SipiSize::MAXDIM);

  size_t w, h;
  int reduce;
  bool reduce_only;

  size.get_size(400, 300, w, h, reduce, reduce_only);
  EXPECT_TRUE(w == 200 && h == 150 && reduce == 1 && reduce_only == 1);
}
