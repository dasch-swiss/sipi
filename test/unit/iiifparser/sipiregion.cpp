#include "gtest/gtest.h"

#include "iiifparser/SipiRegion.h"

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

// Characterization of crop_coords under a reduce divisor (set_reduce). Only the
// COORDS branch currently divides its coordinates by `reduce`; the SQUARE and
// PERCENTS branches ignore it (see SipiRegion::crop_coords). These lock in the
// asymmetric behavior the TIFF pyramid-level decode relies on. `reduce` here is
// the pyramid divisor (1, 2, 4, 8), not the log2 exponent, and the passed nx/ny
// are the reduced-level dimensions.

TEST(SipiRegion, CoordsReduceDividesCoordinates)
{
  auto region = Sipi::SipiRegion("0,0,400,300");
  region.set_reduce(2.F);
  int x, y;
  size_t w, h;
  // A /2 level of a 400x300 source is 200x150; the full-res ROI divides down.
  region.crop_coords(200, 150, x, y, w, h);
  EXPECT_EQ(x, 0);
  EXPECT_EQ(y, 0);
  EXPECT_EQ(w, 200u);// 400 / 2
  EXPECT_EQ(h, 150u);// 300 / 2
}

TEST(SipiRegion, PercentIgnoresReduce)
{
  auto region = Sipi::SipiRegion("pct:0,0,50,50");
  region.set_reduce(2.F);
  int x, y;
  size_t w, h;
  region.crop_coords(200, 150, x, y, w, h);
  // reduce is NOT applied to PERCENTS: percentages are scale-invariant, so this
  // is 50% of the passed (already-reduced) dims.
  EXPECT_EQ(x, 0);
  EXPECT_EQ(y, 0);
  EXPECT_EQ(w, 100u);
  EXPECT_EQ(h, 75u);
}

TEST(SipiRegion, SquareIgnoresReduce)
{
  auto region = Sipi::SipiRegion("square");
  region.set_reduce(2.F);
  int x, y;
  size_t w, h;
  region.crop_coords(200, 150, x, y, w, h);
  // reduce is NOT applied to SQUARE: centered square of the passed dims.
  EXPECT_EQ(x, 25);
  EXPECT_EQ(y, 0);
  EXPECT_EQ(w, 150u);
  EXPECT_EQ(h, 150u);
}

// The default reduce is 1.0 (no reduction); crop_coords must be identical to
// calling it with no set_reduce at all.
TEST(SipiRegion, CoordsReduceOneIsIdentity)
{
  auto region = Sipi::SipiRegion("10,10,400,300");
  region.set_reduce(1.F);
  int x, y;
  size_t w, h;
  region.crop_coords(400, 300, x, y, w, h);
  EXPECT_TRUE(x == 10 && y == 10 && w == 390 && h == 290);
}

// Cache-key stability: the canonical URL crops on FULL-resolution dims with the
// default reduce=1, while the TIFF decode path crops on the reduced LEVEL dims
// with set_reduce(ratio). Both must describe the same physical region so the
// on-disk cache key matches the bytes decoded. For every region kind the
// level-space crop must equal the full-resolution crop divided by the ratio.
TEST(SipiRegion, FullResAndLevelCropsDescribeSameRegion)
{
  const size_t full_w = 400, full_h = 300;
  const float ratio = 2.F;// a /2 pyramid level
  const size_t lvl_w = full_w / static_cast<size_t>(ratio);
  const size_t lvl_h = full_h / static_cast<size_t>(ratio);

  for (const char *spec : { "40,30,200,150", "square", "pct:10,10,50,50" }) {
    auto canonical = Sipi::SipiRegion(spec);// reduce defaults to 1
    auto decode = Sipi::SipiRegion(spec);
    decode.set_reduce(ratio);

    int cx, cy, dx, dy;
    size_t cw, ch, dw, dh;
    canonical.crop_coords(full_w, full_h, cx, cy, cw, ch);
    decode.crop_coords(lvl_w, lvl_h, dx, dy, dw, dh);

    EXPECT_EQ(static_cast<size_t>(dx), static_cast<size_t>(cx) / 2) << spec;
    EXPECT_EQ(static_cast<size_t>(dy), static_cast<size_t>(cy) / 2) << spec;
    EXPECT_EQ(dw, cw / 2) << spec;
    EXPECT_EQ(dh, ch / 2) << spec;
  }
}
