/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <stdexcept>

#include "SipiMemoryBudget.h"

using namespace Sipi;

TEST(MemoryBudgetGuardTest, GuardReleasesOnScopeExit)
{
  SipiMemoryBudget budget(1000, MemoryBudgetMode::ENFORCE);

  {
    auto result = budget.try_acquire(500);
    ASSERT_TRUE(result.allowed);
    MemoryBudgetGuard guard(budget, 500, true);
    EXPECT_EQ(budget.used(), 500);
  }// guard destroyed here

  EXPECT_EQ(budget.used(), 0);
}

TEST(MemoryBudgetGuardTest, GuardReleasesOnException)
{
  SipiMemoryBudget budget(1000, MemoryBudgetMode::ENFORCE);

  try {
    auto result = budget.try_acquire(500);
    ASSERT_TRUE(result.allowed);
    MemoryBudgetGuard guard(budget, 500, true);
    EXPECT_EQ(budget.used(), 500);
    throw std::runtime_error("simulated decode failure");
  } catch (const std::runtime_error &) {
    // guard should have released
  }

  EXPECT_EQ(budget.used(), 0);
}

TEST(MemoryBudgetGuardTest, GuardNotAcquiredDoesNotRelease)
{
  SipiMemoryBudget budget(100, MemoryBudgetMode::ENFORCE);

  // Pre-fill to make acquire fail
  ASSERT_TRUE(budget.try_acquire(100).allowed);
  EXPECT_EQ(budget.used(), 100);

  {
    auto result = budget.try_acquire(200);
    ASSERT_FALSE(result.allowed);
    MemoryBudgetGuard guard(budget, 200, false);// not acquired
  }// guard destroyed — should NOT release 200

  EXPECT_EQ(budget.used(), 100);// unchanged
}

TEST(MemoryBudgetGuardTest, GuardMoveSemantics)
{
  SipiMemoryBudget budget(1000, MemoryBudgetMode::ENFORCE);

  {
    auto result = budget.try_acquire(500);
    ASSERT_TRUE(result.allowed);
    MemoryBudgetGuard guard1(budget, 500, true);

    // Move ownership to guard2
    MemoryBudgetGuard guard2(std::move(guard1));

    // guard1 is now empty — should not release on destruction
    EXPECT_EQ(budget.used(), 500);
  }// both guards destroyed, but only guard2 should release

  EXPECT_EQ(budget.used(), 0);
}
