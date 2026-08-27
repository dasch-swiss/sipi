#include "gtest/gtest.h"

#include "util/checked_arith.h"

#include <cstddef>
#include <limits>
#include <optional>

TEST(CheckedBufSize, ComputesProductWhenNoOverflow)
{
  EXPECT_EQ(Sipi::checked_buf_size(4, 5, 3, 2), std::optional<std::size_t>(120));
}

TEST(CheckedBufSize, IdentityWithAllOnes)
{
  EXPECT_EQ(Sipi::checked_buf_size(1, 1, 1, 1), std::optional<std::size_t>(1));
}

TEST(CheckedBufSize, ZeroFactorIsValidNonOverflowResult)
{
  EXPECT_EQ(Sipi::checked_buf_size(0, 100, 3, 2), std::optional<std::size_t>(0));
  EXPECT_EQ(Sipi::checked_buf_size(100, 0, 3, 2), std::optional<std::size_t>(0));
  EXPECT_EQ(Sipi::checked_buf_size(100, 100, 0, 2), std::optional<std::size_t>(0));
  EXPECT_EQ(Sipi::checked_buf_size(100, 100, 3, 0), std::optional<std::size_t>(0));
}

TEST(CheckedBufSize, FirstMultiplicationOverflowIsRejected)
{
  constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
  EXPECT_EQ(Sipi::checked_buf_size(kMax, 2, 1, 1), std::nullopt);
}

TEST(CheckedBufSize, LaterMultiplicationOverflowIsRejected)
{
  constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
  EXPECT_EQ(Sipi::checked_buf_size(kMax, 1, 2, 1), std::nullopt);
  EXPECT_EQ(Sipi::checked_buf_size(kMax, 1, 1, 2), std::nullopt);
}

TEST(CheckedBufSize, DoesNotSaturateOnOverflow)
{
  // A crafted-header-sized overflow must come back as nullopt, never as a
  // clamped SIZE_MAX value that could silently become an allocation size.
  constexpr std::size_t kHuge = std::numeric_limits<std::size_t>::max() / 2 + 1;
  const auto result = Sipi::checked_buf_size(kHuge, 2, 1, 1);
  ASSERT_EQ(result, std::nullopt);
  EXPECT_NE(result, std::optional<std::size_t>(std::numeric_limits<std::size_t>::max()));
}
