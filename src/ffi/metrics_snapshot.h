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
 * the implementing translation unit — carries the layout. The Rust shell mirrors
 * it by hand (`#[repr(C)] SipiMetricsSnapshot` in `src/server-rs/src/ffi.rs`),
 * not via bindgen; the two layouts are held in lock-step by the paired
 * `static_assert` block (below) and the Rust `offset_of!`/`size_of` test.
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

#include <stddef.h>
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

#ifdef __cplusplus
/* Lock-step layout guard — paired with the Rust offset/size_of test in
 * src/server-rs/src/ffi.rs. Every field is 8 bytes wide (uint64_t / int64_t),
 * so there is no packing subtlety, but the guard still catches an accidental
 * field reorder or insertion on either side. LP64 on every supported target. */
static_assert(sizeof(SipiMetricsSnapshot) == 192, "SipiMetricsSnapshot size drifted from src/server-rs/src/ffi.rs");
static_assert(offsetof(SipiMetricsSnapshot, cache_hits_total) == 0, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, cache_misses_total) == 8, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, cache_evictions_total) == 16, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, cache_skips_total) == 24, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, image_too_large_total) == 32, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, client_disconnected_total) == 40, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, memory_alloc_failures_total) == 48, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, rejected_connections_total) == 56, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, rate_limit_allowed_total) == 64, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, rate_limit_rejected_total) == 72, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, rate_limit_shadow_rejected_total) == 80, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, rate_limit_near_limit_total) == 88, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, decode_memory_acquired_total) == 96, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, decode_memory_rejected_total) == 104, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, decode_memory_shadow_rejected_total) == 112, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, decode_memory_near_limit_total) == 120, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, waiting_connections) == 128, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, cache_size_bytes) == 136, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, cache_files) == 144, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, cache_size_limit_bytes) == 152, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, cache_files_limit) == 160, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, rate_limit_clients_tracked) == 168, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, decode_memory_budget_bytes) == 176, "SipiMetricsSnapshot layout drift");
static_assert(offsetof(SipiMetricsSnapshot, decode_memory_used_bytes) == 184, "SipiMetricsSnapshot layout drift");
#endif

#endif /* SIPI_FFI_METRICS_SNAPSHOT_H */
