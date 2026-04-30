/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef __defined_shttps_connection_metrics_h
#define __defined_shttps_connection_metrics_h

#include <cstddef>

namespace shttps {

// Strategy interface (GoF) for HTTP-layer telemetry hooks. Server holds an
// optional std::shared_ptr<ConnectionMetrics> and delegates per-event to it;
// embedders install a concrete implementation via Server::setMetrics().
//
// Method names are event-shaped because each call site is an event emission;
// the pattern is determined by structure (1:1 delegation, single field,
// interchangeable policy), not by naming. This is *not* the Observer pattern —
// there is no subscriber list, no notify-all, no broadcast.
//
// Lives in the shttps namespace and depends only on <cstddef>; no SIPI types
// are named here. This is the seam that lets shttps stay free of any reverse
// dependency on the SIPI bounded context (see CONTEXT-MAP.md).
class ConnectionMetrics
{
public:
  virtual ~ConnectionMetrics() = default;

  // Called when one or more queued connections are rejected (queue full or
  // wait-time expired). `n` is the count rejected by this event.
  virtual void onConnectionsRejected(std::size_t n) = 0;

  // Called when the depth of the waiting-connections queue changes. `depth`
  // is the new absolute size of the queue.
  virtual void onWaitingConnectionsChanged(std::size_t depth) = 0;

  // Called when a request completes (handler returned without throwing).
  // `duration_seconds` is the wall-clock time spent inside the handler.
  virtual void onRequestComplete(double duration_seconds) = 0;
};

}// namespace shttps

#endif
