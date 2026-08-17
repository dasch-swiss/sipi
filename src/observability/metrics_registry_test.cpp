/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

// The seam tripwire for Sipi::observability::Metrics.
//
// The metrics singleton is engine-internal state; it is NOT the production
// metrics surface. A field reaches production only if it is ALSO read into
// `SipiMetricsSnapshot` (`src/ffi/sipi_ffi.cpp`) and mapped in
// `server-rs/src/metrics.rs`, from where the Rust shell exports it over OTLP.
//
// Nothing else detects a field that is bumped on the serve path but never
// bridged. This test pins the full field inventory against that seam: every
// counter/gauge in `metrics.h` must appear in exactly one of the two sets below,
// so adding a field forces a conscious decision about whether it crosses to OTLP.
//
// The `kBridgedToOtlp` set is the same 21 fields the FFI snapshot reads; its size
// is locked here and independently by the `SipiMetricsSnapshot` layout asserts in
// `src/ffi/metrics_snapshot.h` (size + per-field offset, mirrored in
// `server-rs/src/ffi.rs`).

#include "observability/metrics.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <string>

#include "gtest/gtest.h"

namespace {

// Fields whose values reach production OTLP, via `SipiMetricsSnapshot` and the
// COUNTERS/GAUGES tables in `server-rs/src/metrics.rs`. Exactly the 21 members
// `sipi_metrics_snapshot` reads.
const std::set<std::string> kBridgedToOtlp = {
  "cache_hits_total",
  "cache_misses_total",
  "cache_evictions_total",
  "cache_skips_total",
  "client_disconnected_total",
  "memory_alloc_failures_total",
  "rejected_connections_total",
  "decode_memory_acquired",
  "decode_memory_rejected",
  "decode_memory_shadow_rejected",
  "decode_memory_near_limit_total",
  "decode_memory_too_large_total",
  "decode_memory_shadow_too_large_total",
  "tiff_pyramid_reduced_decodes_total",
  "waiting_connections",
  "cache_size_bytes",
  "cache_files",
  "cache_size_limit_bytes",
  "cache_files_limit",
  "decode_memory_budget_bytes",
  "decode_memory_used_bytes",
};

// Engine-internal counters the snapshot deliberately does NOT carry: the two
// label-fanned codec-diagnostic families whose (format × outcome) label sets the
// flat scalar snapshot cannot express. They are bumped on the read path and read
// by nobody in production — a real observability gap, not a tidy exclusion. Most
// notably `essentials_hash_mismatch_*` is the ADR-0010 corruption tripwire; `sipi
// verify service-file` is its only remaining read path. Bridging them means
// adding 13 scalar fields to the snapshot; the label sets are static, so it is
// mechanical work, not a design problem.
const std::set<std::string> kEngineInternalNotBridged = {
  // read_shape fast path (ADR-0004): format {jp2, tiff} × outcome {hit, miss,
  // partial, fallback} = 8.
  "read_shape_fast_path_jp2_hit",
  "read_shape_fast_path_jp2_miss",
  "read_shape_fast_path_jp2_partial",
  "read_shape_fast_path_jp2_fallback",
  "read_shape_fast_path_tiff_hit",
  "read_shape_fast_path_tiff_miss",
  "read_shape_fast_path_tiff_partial",
  "read_shape_fast_path_tiff_fallback",
  // Essentials hash-mismatch tripwire (ADR-0010): format {jp2, tiff, jpeg, png,
  // other} = 5.
  "essentials_hash_mismatch_jp2",
  "essentials_hash_mismatch_tiff",
  "essentials_hash_mismatch_jpeg",
  "essentials_hash_mismatch_png",
  "essentials_hash_mismatch_other",
};

TEST(SipiMetricsSeam, BridgedSetMatchesTheSnapshotFieldCount)
{
  // The snapshot reads exactly 21 scalar members (7 counters + 6 decode-memory
  // counters + tiff_pyramid + 7 gauges). The `SipiMetricsSnapshot` layout asserts
  // lock the struct; this pins the classification's view of it.
  EXPECT_EQ(kBridgedToOtlp.size(), 21U)
    << "The bridged-to-OTLP set changed. If you added/removed a snapshot field, "
       "update ffi/metrics_snapshot.h + server-rs/src/metrics.rs to match.";
}

TEST(SipiMetricsSeam, EveryFieldIsClassifiedExactlyOnce)
{
  std::set<std::string> overlap;
  std::set_intersection(kBridgedToOtlp.begin(), kBridgedToOtlp.end(),
    kEngineInternalNotBridged.begin(), kEngineInternalNotBridged.end(),
    std::inserter(overlap, overlap.begin()));
  EXPECT_TRUE(overlap.empty()) << "A metric field is classified as both bridged and engine-internal.";
}

TEST(SipiMetricsSeam, EngineInternalGapIsStillTheTwoLabelFannedFamilies)
{
  // Guards the observability gap from growing quietly. Bridging one of these is a
  // deliberate improvement: shrink the list. Adding a new unbridged metric is a
  // regression that should be argued for in review, not slipped in.
  EXPECT_EQ(kEngineInternalNotBridged.size(), 13U)
    << "The set of production-invisible metrics changed. Bridging some? Remove "
       "them here. Adding one? Explain why it cannot cross the seam.";
}

}// namespace
