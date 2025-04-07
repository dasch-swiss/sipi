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
  size_t w, h;
  bool reduce_only;

  {
    int reduce = 10000;
    EXPECT_TRUE(Sipi::SipiSize("pct:25").get_size(400, 300, w, h, reduce, reduce_only) == Sipi::SipiSize::PERCENTS);
    EXPECT_TRUE(w == 100 && h == 75 && reduce == 2) << w << "/" << h << "/" << reduce << "/" << reduce_only;
  }

  {
    int reduce = 10000;
    EXPECT_TRUE(Sipi::SipiSize("pct:10").get_size(400, 300, w, h, reduce, reduce_only) == Sipi::SipiSize::PERCENTS);
    EXPECT_TRUE(w == 40 && h == 30 && reduce == 3) << w << "/" << h << "/" << reduce << "/" << reduce_only;
  }
}

TEST(SipiSize, BangMaxdim)
{
  auto size = Sipi::SipiSize("!200,200");
  EXPECT_TRUE(size.getType() == Sipi::SipiSize::MAXDIM);

  size_t w, h;
  int reduce;
  bool reduce_only;

  size.get_size(400, 300, w, h, reduce, reduce_only);
  EXPECT_TRUE(w == 200 && h == 150 && reduce == 1 && reduce_only == 0);
}

TEST(SipiSize, Full)
{
  auto size = Sipi::SipiSize("max");
  EXPECT_TRUE(size.getType() == Sipi::SipiSize::FULL);

  size_t w, h;
  int reduce;
  bool reduce_only;

  size.get_size(400, 300, w, h, reduce, reduce_only);
  EXPECT_TRUE(w == 400 && h == 300 && reduce == 0 && reduce_only == 1);
}
