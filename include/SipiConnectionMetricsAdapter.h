/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef __defined_sipi_connection_metrics_adapter_h
#define __defined_sipi_connection_metrics_adapter_h

#include "shttps/ConnectionMetrics.h"

namespace Sipi {

// Adapter (GoF): bridges the shttps::ConnectionMetrics strategy interface to
// the legacy SipiMetrics singleton. SIPI installs an instance on the shttps
// Server at startup; from then on shttps delegates telemetry events here
// without naming any SIPI type.
class SipiConnectionMetricsAdapter : public shttps::ConnectionMetrics
{
public:
  void onConnectionsRejected(std::size_t n) override;
  void onWaitingConnectionsChanged(std::size_t depth) override;
  void onRequestComplete(double duration_seconds) override;
};

}// namespace Sipi

#endif
