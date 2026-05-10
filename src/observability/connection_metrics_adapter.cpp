/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "observability/connection_metrics_adapter.h"
#include "observability/metrics.h"

namespace Sipi::observability {

void ConnectionMetricsAdapter::onConnectionsRejected(std::size_t n)
{
  Metrics::instance().rejected_connections_total.Increment(static_cast<double>(n));
}

void ConnectionMetricsAdapter::onWaitingConnectionsChanged(std::size_t depth)
{
  Metrics::instance().waiting_connections.Set(static_cast<double>(depth));
}

void ConnectionMetricsAdapter::onRequestComplete(double duration_seconds)
{
  Metrics::instance().request_duration_seconds.Observe(duration_seconds);
}

}// namespace Sipi::observability
