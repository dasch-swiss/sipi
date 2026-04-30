/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

// Unit tests for Sipi::SipiConnectionMetricsAdapter — the GoF Adapter that
// bridges shttps's ConnectionMetrics strategy interface to the legacy
// SipiMetrics singleton. Each test fires one callback and asserts the
// expected mutation on SipiMetrics::instance() via a before/after delta
// (the singleton is process-global, so absolute values are not reliable).

#include "SipiConnectionMetricsAdapter.h"
#include "SipiMetrics.h"

#include "gtest/gtest.h"

namespace {

TEST(SipiConnectionMetricsAdapter, OnConnectionsRejectedIncrementsCounterByN)
{
  Sipi::SipiConnectionMetricsAdapter adapter;
  auto &counter = SipiMetrics::instance().rejected_connections_total;

  const double before = counter.Value();
  adapter.onConnectionsRejected(7);
  const double after = counter.Value();

  EXPECT_DOUBLE_EQ(after - before, 7.0);
}

TEST(SipiConnectionMetricsAdapter, OnWaitingConnectionsChangedSetsGaugeAbsolute)
{
  Sipi::SipiConnectionMetricsAdapter adapter;
  auto &gauge = SipiMetrics::instance().waiting_connections;

  // Gauge::Set is absolute: the post-call value equals the supplied depth
  // regardless of prior state.
  adapter.onWaitingConnectionsChanged(42);
  EXPECT_DOUBLE_EQ(gauge.Value(), 42.0);

  adapter.onWaitingConnectionsChanged(0);
  EXPECT_DOUBLE_EQ(gauge.Value(), 0.0);
}

TEST(SipiConnectionMetricsAdapter, OnRequestCompleteRecordsHistogramObservation)
{
  Sipi::SipiConnectionMetricsAdapter adapter;
  auto &histogram = SipiMetrics::instance().request_duration_seconds;

  const auto before = histogram.Collect();
  adapter.onRequestComplete(0.125);
  const auto after = histogram.Collect();

  ASSERT_EQ(before.histogram.sample_count + 1, after.histogram.sample_count);
  EXPECT_DOUBLE_EQ(after.histogram.sample_sum - before.histogram.sample_sum, 0.125);
}

}// namespace
