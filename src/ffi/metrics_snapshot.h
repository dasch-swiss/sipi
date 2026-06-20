/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * Concrete layout for the `SipiMetricsSnapshot` handle forward-declared in
 * `ffi/sipi_ffi.h` (strangler-fig Phase B; ADR-0013).
 *
 * `sipi_metrics_snapshot` ferries the engine's `Sipi::observability::Metrics`
 * singleton counters/gauges across the seam so the Phase C Rust shell can feed
 * them to an OTel meter (the C++ Prometheus `/metrics` handler stays until the
 * cutover, then retires — OTLP-only). The seam header keeps this struct opaque
 * so the contract commits no field set it doesn't need; this header — owned by
 * the implementing translation unit — carries the layout, and Phase C's bindgen
 * reads it here.
 *
 * Plain C, flat by design: every field is a single number, so the Rust meter
 * binds each to a counter/gauge instrument without parsing. **Counters** are
 * monotonic → `uint64_t`. **Gauges** → `int64_t` (signed because
 * `cache_size_limit_bytes` uses `-1` for "unlimited").
 *
 * **Inclusion rule** (so an omission reads as deliberate, not drift): this
 * snapshot carries the shell's serve-path operational signals — cache,
 * rate-limit, decode-memory budget, and request-admission counters/gauges. A
 * new *scalar serve-path* counter/gauge on the singleton should gain a field
 * here. Deliberately out: histograms (`request_duration_seconds`,
 * `decode_memory_estimate_bytes` — OTel records these with its own native
 * instrument from the live request path), the `build_info` marker (becomes an
 * OTel resource attribute at init), and the label-fanned codec-diagnostic
 * families (`read_shape_fast_path_*`, `essentials_hash_mismatch_*` — per-format
 * engine tripwires per ADR-0004/0010, not shell RED signals).
 */
#ifndef SIPI_FFI_METRICS_SNAPSHOT_H
#define SIPI_FFI_METRICS_SNAPSHOT_H

#include <stdint.h>

#include "ffi/sipi_ffi.h"

/*! Completes the opaque `SipiMetricsSnapshot` from `ffi/sipi_ffi.h`. */
struct SipiMetricsSnapshot
{
  /* ── Counters (monotonic) ───────────────────────────────────────────────── */
  uint64_t cache_hits_total;
  uint64_t cache_misses_total;
  uint64_t cache_evictions_total;
  uint64_t cache_skips_total;
  uint64_t image_too_large_total;
  uint64_t client_disconnected_total;
  uint64_t memory_alloc_failures_total;
  uint64_t rejected_connections_total;

  uint64_t rate_limit_allowed_total;
  uint64_t rate_limit_rejected_total;
  uint64_t rate_limit_shadow_rejected_total;
  uint64_t rate_limit_near_limit_total;

  uint64_t decode_memory_acquired_total;
  uint64_t decode_memory_rejected_total;
  uint64_t decode_memory_shadow_rejected_total;
  uint64_t decode_memory_near_limit_total;

  /* ── Gauges (signed: cache_size_limit_bytes uses -1 = unlimited) ─────────── */
  int64_t waiting_connections;
  int64_t cache_size_bytes;
  int64_t cache_files;
  int64_t cache_size_limit_bytes;
  int64_t cache_files_limit;
  int64_t rate_limit_clients_tracked;
  int64_t decode_memory_budget_bytes;
  int64_t decode_memory_used_bytes;
};

#endif /* SIPI_FFI_METRICS_SNAPSHOT_H */
