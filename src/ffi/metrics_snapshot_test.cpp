/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gtest/gtest.h"

#include "ffi/metrics_snapshot.h"
#include "ffi/sipi_ffi.h"
#include "observability/metrics.h"

namespace {

using Sipi::observability::Metrics;

// The Metrics singleton is process-wide, so other TESTs in this binary may have
// touched it. Counter assertions are on deltas (never absolutes); gauge
// assertions Set-then-read in the same test so the value is deterministic.

TEST(MetricsSnapshot, CountersReflectIncrements)
{
  auto &m = Metrics::instance();

  SipiMetricsSnapshot before{};
  ASSERT_EQ(sipi_metrics_snapshot(&before), 0);

  m.cache_hits_total.Increment();
  m.cache_hits_total.Increment();
  m.cache_misses_total.Increment();
  m.decode_memory_acquired.Increment();
  m.rate_limit_decisions_total.Add({ { "action", "allowed" } }).Increment();

  SipiMetricsSnapshot after{};
  ASSERT_EQ(sipi_metrics_snapshot(&after), 0);

  EXPECT_EQ(after.cache_hits_total - before.cache_hits_total, 2u);
  EXPECT_EQ(after.cache_misses_total - before.cache_misses_total, 1u);
  EXPECT_EQ(after.decode_memory_acquired_total - before.decode_memory_acquired_total, 1u);
  EXPECT_EQ(after.rate_limit_allowed_total - before.rate_limit_allowed_total, 1u);
}

TEST(MetricsSnapshot, GaugesReflectCurrentValue)
{
  auto &m = Metrics::instance();

  m.cache_size_bytes.Set(4096);
  m.cache_files.Set(7);
  m.cache_size_limit_bytes.Set(-1);// -1 = unlimited — exercises the signed gauge

  SipiMetricsSnapshot snap{};
  ASSERT_EQ(sipi_metrics_snapshot(&snap), 0);

  EXPECT_EQ(snap.cache_size_bytes, 4096);
  EXPECT_EQ(snap.cache_files, 7);
  EXPECT_EQ(snap.cache_size_limit_bytes, -1);
}

}// namespace
