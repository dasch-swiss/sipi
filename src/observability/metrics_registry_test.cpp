/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

// The seam tripwire for Sipi::observability::Metrics.
//
// The engine's Prometheus registry is NOT the production metrics surface. `GET
// /metrics` serialises it on the retained C++ server alone, which is oracle-only
// and never deployed; in production the Rust shell exports over OTLP, and only
// what crosses the FFI seam gets there. So a metric added to this registry
// reaches production only if it is ALSO wired into `ffi/metrics_snapshot.h` and
// `server-rs/src/metrics.rs`.
//
// Nothing else detects that omission. It is how `sipi_request_duration_seconds`
// and `sipi_decode_memory_estimate_bytes` came to be incremented on every serve
// into a registry nobody read. This test pins the registry's family names so
// adding one forces a decision about the seam, recorded in the tables below.

#include "observability/metrics.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {

using Sipi::observability::Metrics;

// Families whose values reach production OTLP, via `SipiMetricsSnapshot` and the
// COUNTERS/GAUGES tables in `server-rs/src/metrics.rs`.
const std::set<std::string> kBridgedToOtlp = {
  "sipi_cache_hits_total",
  "sipi_cache_misses_total",
  "sipi_cache_evictions_total",
  "sipi_cache_skips_total",
  "sipi_image_too_large_total",
  "sipi_client_disconnected_total",
  "sipi_memory_alloc_failures_total",
  "sipi_rate_limit_near_limit_total",
  "sipi_decode_memory_near_limit_total",
  "sipi_cache_size_bytes",
  "sipi_cache_files",
  "sipi_cache_size_limit_bytes",
  "sipi_cache_files_limit",
  "sipi_rate_limit_clients_tracked",
  "sipi_decode_memory_budget_bytes",
  "sipi_decode_memory_used_bytes",
  "sipi_tiff_pyramid_reduced_decodes_total",
};

// Label-fanned families whose pre-created children are bridged individually,
// flattened into one unlabelled OTel instrument each (the snapshot carries
// scalars, not label sets). Operator PromQL must use the flattened names —
// `..._decisions_total{action="rejected"}` matches nothing in production.
const std::set<std::string> kBridgedFlattened = {
  "sipi_rate_limit_decisions_total",// → sipi.rate_limit.{allowed,rejected,shadow_rejected}
  "sipi_decode_memory_decisions_total",// → sipi.decode_memory.{acquired,rejected,shadow_rejected}
};

// Families the shell deliberately re-implements instead of bridging, because
// they describe the transport and only the oracle's transport writes them. Under
// the Rust shell these stay zero forever; its own pool publishes the analogues
// (`sipi.pool.waiting`, `sipi.pool.load_shed`, `sipi.pool.queue_timeout`).
const std::set<std::string> kReplacedByShell = {
  "sipi_waiting_connections",
  "sipi_rejected_connections_total",
  // Observed only by ConnectionMetricsAdapter, i.e. never on the serve path. The
  // shell records the semconv `http.server.request.duration` histogram itself.
  "sipi_request_duration_seconds",
};

// Carried across the seam but recorded shell-side, because a histogram cannot
// cross the flat snapshot: the engine hands the sample back on
// `SipiServeTimings::decode_estimate_bytes` and the shell records it into
// `sipi.decode_memory.estimate_bytes`.
const std::set<std::string> kRecordedShellSide = {
  "sipi_decode_memory_estimate_bytes",
};

// Not a metric in production: the build stamp travels as the OTel resource
// attributes `service.version` + `vcs.ref.head.revision` (see
// `server-rs/src/telemetry.rs`), which is the OTLP-native shape for build
// identity and avoids a constant-1 gauge.
const std::set<std::string> kResourceAttributes = {
  "sipi_build_info",
};

// KNOWN EXCEPTIONS — incremented in production and read by nobody.
//
// Both are label-fanned codec diagnostics whose label sets the flat
// `SipiMetricsSnapshot` cannot express. Unlike the decisions families above,
// their children are numerous enough (8 and 5) that flattening them into
// individual scalars was deferred rather than done. Bridging them means adding
// 13 scalar fields to the snapshot; the label sets are static and pre-created,
// so it is mechanical work, not a design problem.
//
// This is a real observability gap, not a tidy exclusion. Most notably
// `sipi_essentials_hash_mismatch_total` is the ADR-0010 corruption tripwire:
// nothing in production can currently observe a detected corruption, and `sipi
// verify service-file` is the only remaining read path.
const std::set<std::string> kKnownExceptionsNotObservable = {
  "sipi_read_shape_fast_path_total",// ADR-0004; format × outcome = 8 children
  "sipi_essentials_hash_mismatch_total",// ADR-0010 corruption tripwire; 5 children
};

//! Every family name the engine registry currently exposes.
std::set<std::string> registered_families()
{
  std::set<std::string> names;
  for (const auto &family : Metrics::instance().registry()->Collect()) { names.insert(family.name); }
  return names;
}

//! The union of every classification above.
std::set<std::string> classified_families()
{
  std::set<std::string> all;
  for (const auto *group : { &kBridgedToOtlp,
         &kBridgedFlattened,
         &kReplacedByShell,
         &kRecordedShellSide,
         &kResourceAttributes,
         &kKnownExceptionsNotObservable }) {
    all.insert(group->begin(), group->end());
  }
  return all;
}

//! Names in `lhs` that are absent from `rhs`, space-joined for the failure message.
std::string only_in(const std::set<std::string> &lhs, const std::set<std::string> &rhs)
{
  std::vector<std::string> difference;
  std::set_difference(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), std::back_inserter(difference));
  std::string joined;
  for (const auto &name : difference) { joined += name + " "; }
  return joined;
}

TEST(SipiMetricsRegistry, EveryFamilyIsClassifiedAgainstTheSeam)
{
  const std::string unclassified = only_in(registered_families(), classified_families());

  EXPECT_TRUE(unclassified.empty()) << "New metric family in the engine registry, unclassified: " << unclassified
                                    << "\nThe registry is NOT the production surface. Decide how this metric "
                                       "reaches OTLP — add it to ffi/metrics_snapshot.h AND "
                                       "server-rs/src/metrics.rs, then list it in kBridgedToOtlp; or record it "
                                       "shell-side; or add it to a known-exception list with the reason.";
}

TEST(SipiMetricsRegistry, NoClassificationEntryIsStale)
{
  const std::string missing = only_in(classified_families(), registered_families());

  EXPECT_TRUE(missing.empty()) << "Classified but no longer registered (renamed or removed — drop the entry): "
                               << missing;
}

TEST(SipiMetricsRegistry, KnownExceptionsAreStillTheOnlyUnobservableFamilies)
{
  // Guards the gap from growing quietly. Bridging one of these is a deliberate
  // improvement: shrink the list. Adding one is a regression that should be
  // argued for in review, not slipped in.
  EXPECT_EQ(kKnownExceptionsNotObservable.size(), 2U)
    << "The set of production-invisible metrics changed. Bridging one? Remove "
       "it here. Adding one? Explain why it cannot cross the seam.";
}

}// namespace
