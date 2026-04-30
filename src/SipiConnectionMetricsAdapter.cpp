/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "SipiConnectionMetricsAdapter.h"
#include "SipiMetrics.h"

namespace Sipi {

void SipiConnectionMetricsAdapter::onConnectionsRejected(std::size_t n)
{
  SipiMetrics::instance().rejected_connections_total.Increment(static_cast<double>(n));
}

void SipiConnectionMetricsAdapter::onWaitingConnectionsChanged(std::size_t depth)
{
  SipiMetrics::instance().waiting_connections.Set(static_cast<double>(depth));
}

void SipiConnectionMetricsAdapter::onRequestComplete(double duration_seconds)
{
  SipiMetrics::instance().request_duration_seconds.Observe(duration_seconds);
}

}// namespace Sipi
