#include "gtest/gtest.h"

#include "../../../include/iiifparser/SipiRegion.h"

TEST(SipiRegion, Full)
{
  EXPECT_TRUE(Sipi::SipiRegion("").getType() == Sipi::SipiRegion::FULL);
  EXPECT_TRUE(Sipi::SipiRegion("full").getType() == Sipi::SipiRegion::FULL);

  auto region = Sipi::SipiRegion("full");
  int x, y;
  size_t w, h;
  region.crop_coords(400, 300, x, y, w, h);
  EXPECT_TRUE(x == 0 && y == 0 && w == 400 && h == 300);
}

TEST(SipiRegion, Square)
{
  auto region = Sipi::SipiRegion("square");
  int x, y;
  size_t w, h;
  region.crop_coords(400, 300, x, y, w, h);
  EXPECT_TRUE(x == 50 && y == 0 && w == 300 && h == 300);
}

TEST(SipiRegion, Percent)
{
  auto region = Sipi::SipiRegion("pct:10,10,50,50");
  int x, y;
  size_t w, h;
  region.crop_coords(400, 300, x, y, w, h);
  EXPECT_TRUE(x == 40 && y == 30 && w == 200 && h == 150);
}

TEST(SipiRegion, Coords)
{
  auto region = Sipi::SipiRegion("10,10,400,300");
  int x, y;
  size_t w, h;

  region.crop_coords(400, 300, x, y, w, h);
  EXPECT_TRUE(x == 10 && y == 10 && w == 390 && h == 290);

  region.crop_coords(800, 400, x, y, w, h);
  EXPECT_TRUE(x == 10 && y == 10 && w == 400 && h == 300);
}
