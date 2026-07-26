#include "gtest/gtest.h"

#include "iiifparser/SipiSize.h"

TEST(SipiSize, PixelsXY)
{
  auto size = Sipi::SipiSize("400,300");
  EXPECT_TRUE(size.getType() == Sipi::SipiSize::PIXELS_XY);

  size_t w, h;
  int reduce = 10000;
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
  int reduce = 10000;
  bool reduce_only;

  size.get_size(400, 300, w, h, reduce, reduce_only);
  EXPECT_TRUE(w == 200 && h == 150 && reduce == 1 && reduce_only == 1);
}

TEST(SipiSize, Full)
{
  auto size = Sipi::SipiSize("max");
  EXPECT_TRUE(size.getType() == Sipi::SipiSize::FULL);

  size_t w, h;
  int reduce = 10000;
  bool reduce_only;

  size.get_size(400, 300, w, h, reduce, reduce_only);
  EXPECT_TRUE(w == 400 && h == 300 && reduce == 0 && reduce_only == 1);
}

// Characterization of the reduce EXPONENT that get_size hands to the TIFF
// pyramid-level selector. get_size returns `reduce` as a log2 exponent
// (0, 1, 2, 3 => divisor 1, 2, 4, 8) and `reduce_only` = true only when the
// requested size lands exactly on a power-of-two reduce with no residual scale.
// The TIFF pyramid-level selector consumes exactly these two out-params to pick
// a pyramid IFD, so lock in these values.

// pct:50 == 1/2 => exponent 1, exact => reduce_only.
TEST(SipiSize, PercentHalfIsExactReduceLevelOne)
{
  size_t w, h;
  int reduce = 10000;
  bool reduce_only = false;
  EXPECT_TRUE(Sipi::SipiSize("pct:50").get_size(400, 300, w, h, reduce, reduce_only)
              == Sipi::SipiSize::PERCENTS);
  EXPECT_EQ(w, 200u);
  EXPECT_EQ(h, 150u);
  EXPECT_EQ(reduce, 1);
  EXPECT_TRUE(reduce_only);
}

// pct:37 sits BETWEEN levels 1 (1/2) and 2 (1/4). Current behavior: the largest
// exponent whose divisor still fits (exponent 1, divisor 2, since 2*1 <= 100/37
// but 2*2 > 100/37), and reduce_only == false (a residual scale is required).
TEST(SipiSize, PercentBetweenLevelsIsNotReduceOnly)
{
  size_t w, h;
  int reduce = 10000;
  bool reduce_only = true;
  EXPECT_TRUE(Sipi::SipiSize("pct:37").get_size(400, 300, w, h, reduce, reduce_only)
              == Sipi::SipiSize::PERCENTS);
  EXPECT_EQ(reduce, 1);
  EXPECT_FALSE(reduce_only);
}

// The pyramid selector is bounded by max_reduce (the IN value of `reduce`,
// derived from the number of available levels). A deep reduce request is clamped
// so the selector never indexes past the smallest level.
TEST(SipiSize, PercentReduceClampedByMaxReduce)
{
  size_t w, h;
  int reduce = 2;// only levels 0..2 available (divisors 1, 2, 4)
  bool reduce_only = false;
  // pct:5 wants ~1/20 (exponent 4); clamp to max_reduce == 2.
  Sipi::SipiSize("pct:5").get_size(4000, 3000, w, h, reduce, reduce_only);
  EXPECT_EQ(reduce, 2);
}
