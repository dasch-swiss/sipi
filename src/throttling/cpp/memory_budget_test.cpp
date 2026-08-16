/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <atomic>
#include <optional>
#include <thread>
#include <vector>

#include "SipiMemoryBudget.h"

using namespace Sipi;

// --- Mode parsing ---

TEST(AdmissionModeTest, ParseEnforce)
{
  EXPECT_EQ(parse_admission_mode("enforce"), AdmissionMode::ENFORCE);
  EXPECT_EQ(parse_admission_mode("ENFORCE"), AdmissionMode::ENFORCE);
}

TEST(AdmissionModeTest, ParseMonitor)
{
  EXPECT_EQ(parse_admission_mode("monitor"), AdmissionMode::MONITOR);
  EXPECT_EQ(parse_admission_mode("MONITOR"), AdmissionMode::MONITOR);
}

TEST(AdmissionModeTest, ParseUnknownReturnsNullopt)
{
  // There is no "off"; a stale/legacy value is a startup error, so the parser
  // signals it with nullopt (the caller fails loud) rather than defaulting.
  EXPECT_EQ(parse_admission_mode("off"), std::nullopt);
  EXPECT_EQ(parse_admission_mode("invalid"), std::nullopt);
  EXPECT_EQ(parse_admission_mode(""), std::nullopt);
}

// --- Acquire / Release in ENFORCE mode ---

TEST(MemoryBudgetTest, AcquireWithinBudgetSucceeds)
{
  SipiMemoryBudget budget(1000, AdmissionMode::ENFORCE);
  auto result = budget.try_acquire(100);
  EXPECT_TRUE(result.allowed);
  EXPECT_FALSE(result.over_budget);
  EXPECT_FALSE(result.exceeds_budget_alone);
  EXPECT_EQ(result.used, 100);
  EXPECT_EQ(result.budget, 1000);
}

// A single request larger than the whole budget → permanently unservable (413).
TEST(MemoryBudgetTest, SingleRequestExceedingBudgetIsFlaggedAsTooLarge)
{
  SipiMemoryBudget budget(1000, AdmissionMode::ENFORCE);
  auto result = budget.try_acquire(1100);
  EXPECT_FALSE(result.allowed);
  EXPECT_TRUE(result.over_budget);
  EXPECT_TRUE(result.exceeds_budget_alone);// 1100 alone > 1000 → 413, not 503
  EXPECT_EQ(result.used, 0);// not acquired
  EXPECT_EQ(budget.used(), 0);
}

// Load-driven over-budget: the request fits alone but not alongside current
// usage → transient (503), NOT flagged as too-large.
TEST(MemoryBudgetTest, TransientOverBudgetIsNotFlaggedAsTooLarge)
{
  SipiMemoryBudget budget(1000, AdmissionMode::ENFORCE);
  ASSERT_TRUE(budget.try_acquire(900).allowed);
  auto result = budget.try_acquire(200);// 200 alone fits, but 900+200 > 1000
  EXPECT_FALSE(result.allowed);
  EXPECT_TRUE(result.over_budget);
  EXPECT_FALSE(result.exceeds_budget_alone);// 200 <= 1000 → 503, not 413
  EXPECT_EQ(budget.used(), 900);// unchanged
}

// --- MONITOR mode shadow-counts but always admits ---

TEST(MemoryBudgetTest, MonitorShadowCountsTransientOverBudget)
{
  SipiMemoryBudget budget(1000, AdmissionMode::MONITOR);
  ASSERT_TRUE(budget.try_acquire(900).allowed);
  auto result = budget.try_acquire(200);
  EXPECT_TRUE(result.allowed);// monitor always admits
  EXPECT_TRUE(result.over_budget);// but shadow-counts the would-be 503
  EXPECT_FALSE(result.exceeds_budget_alone);
  EXPECT_EQ(result.used, 1100);// tracked even in monitor mode
  EXPECT_EQ(budget.used(), 1100);
}

TEST(MemoryBudgetTest, MonitorShadowCountsTooLargeSingleRequest)
{
  SipiMemoryBudget budget(1000, AdmissionMode::MONITOR);
  auto result = budget.try_acquire(1100);
  EXPECT_TRUE(result.allowed);// monitor always admits
  EXPECT_TRUE(result.over_budget);
  EXPECT_TRUE(result.exceeds_budget_alone);// shadow-counts the would-be 413
  EXPECT_EQ(budget.used(), 1100);
}

TEST(MemoryBudgetTest, ReleaseRestoresBudget)
{
  SipiMemoryBudget budget(1000, AdmissionMode::ENFORCE);
  ASSERT_TRUE(budget.try_acquire(500).allowed);
  EXPECT_EQ(budget.used(), 500);
  budget.release(500);
  EXPECT_EQ(budget.used(), 0);
}

TEST(MemoryBudgetTest, MultipleAcquiresAccumulate)
{
  SipiMemoryBudget budget(1000, AdmissionMode::ENFORCE);
  ASSERT_TRUE(budget.try_acquire(300).allowed);
  ASSERT_TRUE(budget.try_acquire(300).allowed);
  ASSERT_TRUE(budget.try_acquire(300).allowed);
  EXPECT_EQ(budget.used(), 900);

  // Next 200 would exceed 1000
  auto result = budget.try_acquire(200);
  EXPECT_FALSE(result.allowed);
  EXPECT_EQ(budget.used(), 900);// unchanged
}

TEST(MemoryBudgetTest, ReleaseMoreThanAcquiredClampsToZero)
{
  SipiMemoryBudget budget(1000, AdmissionMode::ENFORCE);
  ASSERT_TRUE(budget.try_acquire(100).allowed);
  budget.release(2000);// more than acquired
  EXPECT_EQ(budget.used(), 0);// clamped, no underflow
}

TEST(MemoryBudgetTest, ZeroBytesAcquireAlwaysSucceeds)
{
  SipiMemoryBudget budget(1000, AdmissionMode::ENFORCE);
  ASSERT_TRUE(budget.try_acquire(1000).allowed);
  // Zero-byte acquire should still succeed
  auto result = budget.try_acquire(0);
  EXPECT_TRUE(result.allowed);
  EXPECT_FALSE(result.over_budget);
  EXPECT_FALSE(result.exceeds_budget_alone);
}

TEST(MemoryBudgetTest, ExactBudgetAcquireSucceeds)
{
  SipiMemoryBudget budget(1000, AdmissionMode::ENFORCE);
  auto result = budget.try_acquire(1000);
  EXPECT_TRUE(result.allowed);
  EXPECT_FALSE(result.over_budget);
  EXPECT_FALSE(result.exceeds_budget_alone);
  EXPECT_EQ(result.used, 1000);
}

// --- Concurrency tests ---

TEST(MemoryBudgetTest, ConcurrentAcquireRelease)
{
  SipiMemoryBudget budget(1'000'000, AdmissionMode::ENFORCE);
  constexpr int num_threads = 8;
  constexpr int iterations = 1000;
  constexpr size_t bytes_per_op = 100;

  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&budget]() {
      for (int i = 0; i < iterations; ++i) {
        auto result = budget.try_acquire(bytes_per_op);
        if (result.allowed) {
          budget.release(bytes_per_op);
        }
      }
    });
  }

  for (auto &th : threads) {
    th.join();
  }

  EXPECT_EQ(budget.used(), 0);
}

TEST(MemoryBudgetTest, ConcurrentAcquiresRespectBudget)
{
  constexpr size_t total_budget = 1000;
  SipiMemoryBudget budget(total_budget, AdmissionMode::ENFORCE);
  constexpr int num_threads = 8;
  constexpr size_t bytes_per_acquire = 200;// 5 can fit in 1000

  std::atomic<size_t> max_observed_used{0};
  std::atomic<bool> go{false};
  std::atomic<bool> budget_ever_exceeded{false};

  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&]() {
      while (!go.load(std::memory_order_acquire)) {}// spin until all threads ready

      auto result = budget.try_acquire(bytes_per_acquire);
      if (result.allowed) {
        // Observe current usage — should never exceed budget
        size_t current = budget.used();
        if (current > total_budget) {
          budget_ever_exceeded.store(true, std::memory_order_relaxed);
        }
        size_t prev = max_observed_used.load(std::memory_order_relaxed);
        while (current > prev && !max_observed_used.compare_exchange_weak(prev, current, std::memory_order_relaxed)) {}

        // Simulate some work
        std::this_thread::yield();

        budget.release(bytes_per_acquire);
      }
    });
  }

  go.store(true, std::memory_order_release);

  for (auto &th : threads) {
    th.join();
  }

  // The key invariant: concurrent usage never exceeds the budget
  EXPECT_LE(max_observed_used.load(), total_budget);
  EXPECT_FALSE(budget_ever_exceeded.load());
  EXPECT_EQ(budget.used(), 0);
}
