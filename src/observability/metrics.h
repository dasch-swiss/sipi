/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_OBSERVABILITY_METRICS_H
#define SIPI_OBSERVABILITY_METRICS_H

#include <atomic>
#include <cstdint>
#include <string>

/*!
 * FFI snapshot-bridge invariant.
 *
 * `Sipi::observability::Metrics` is engine-internal state, NOT the production
 * metrics surface. A scalar counter/gauge added here reaches production only if
 * it is also read into `SipiMetricsSnapshot` (src/ffi/metrics_snapshot.h — see
 * its "Inclusion rule" block for what belongs) by `sipi_metrics_snapshot`
 * (src/ffi/sipi_ffi.cpp) and mapped in server-rs/src/metrics.rs, from where the
 * Rust shell exports it over OTLP. The co-located seam tripwire
 * `metrics_registry_test.cpp` pins the full field inventory against that bridge,
 * so adding a field forces a conscious decision about whether it crosses to OTLP.
 */

namespace Sipi::observability {

/*!
 * Monotonic counter — a lock-free `std::atomic<uint64_t>`. Every scalar counter
 * the production FFI snapshot (`sipi_metrics_snapshot`) reads is one of these;
 * the Rust shell narrows `Value()` into the OTLP instrument. `Increment(double)`
 * exists so the eviction call sites can add a batch count in one call.
 */
class Counter
{
  std::atomic<std::uint64_t> value_{ 0 };

public:
  void Increment() noexcept { value_.fetch_add(1, std::memory_order_relaxed); }
  void Increment(double n) noexcept
  {
    if (n > 0.0) { value_.fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed); }
  }
  [[nodiscard]] std::uint64_t Value() const noexcept { return value_.load(std::memory_order_relaxed); }
};

/*!
 * Point-in-time gauge — a lock-free `std::atomic<int64_t>` (signed: the cache
 * size limit is -1 when unlimited). `Set` takes a `double` because the cache and
 * memory-budget call sites hold their values as `size_t`/`double`.
 */
class Gauge
{
  std::atomic<std::int64_t> value_{ 0 };

public:
  void Set(double v) noexcept { value_.store(static_cast<std::int64_t>(v), std::memory_order_relaxed); }
  [[nodiscard]] std::int64_t Value() const noexcept { return value_.load(std::memory_order_relaxed); }
};

/*!
 * The engine's metrics singleton. Plain atomic counters and gauges bumped on the
 * decode/cache/serve paths. Scalar fields cross the FFI seam as
 * `SipiMetricsSnapshot` (`src/ffi/sipi_ffi.cpp`) and are exported over OTLP by the
 * Rust shell — see `src/ffi/metrics_snapshot.h`'s inclusion rule for which fields
 * ride the bridge (the label-fanned `read_shape_*` / `essentials_hash_mismatch_*`
 * families are engine-internal and do not).
 */
class Metrics
{
public:
  static Metrics &instance();

  // Counters
  Counter cache_hits_total;
  Counter cache_misses_total;
  Counter cache_evictions_total;
  Counter cache_skips_total;
  Counter client_disconnected_total;
  Counter memory_alloc_failures_total;

  // Queue counters
  Counter rejected_connections_total;

  // Gauges
  Gauge waiting_connections;
  Gauge cache_size_bytes;
  Gauge cache_files;
  Gauge cache_size_limit_bytes;
  Gauge cache_files_limit;

  // Memory budget metrics (full lane). `decode_memory_budget_bytes` is the full
  // lane's byte cap (envelope × (1 − tiles_memory_ratio)); tile decodes bypass
  // the budget and are never charged.
  Gauge decode_memory_budget_bytes;
  Gauge decode_memory_used_bytes;
  Counter decode_memory_acquired;
  Counter decode_memory_rejected;       // advanced: transient over-budget → 503
  Counter decode_memory_shadow_rejected;// basic: would-be 503 (shadow-counted)
  Counter decode_memory_near_limit_total;
  // A single request whose estimate alone exceeds the full-lane budget is
  // permanently unservable → 413 (no Retry-After), distinct from a transient
  // 503. Engine-internal until bridged through SipiMetricsSnapshot.
  Counter decode_memory_too_large_total;       // advanced: 413 returned
  Counter decode_memory_shadow_too_large_total;// basic: would-be 413 (shadow-counted)

  // read_shape fast path (ADR-0004 / DEV-6537).
  // Format = {jp2, tiff}; outcome = {hit, miss, partial, fallback}.
  //   - hit:      Essentials packet parsed; img_w & img_h populated;
  //               fast path returned shape from packet.
  //   - miss:     No Essentials packet found; slow path computed shape.
  //   - partial:  Essentials parsed but only one of img_w/img_h
  //               non-zero; slow path computed shape.
  //   - fallback: Legacy pipe-delimited carrier present (no shape
  //               fields) OR new-carrier parse error; slow path
  //               computed shape.
  Counter read_shape_fast_path_jp2_hit;
  Counter read_shape_fast_path_jp2_miss;
  Counter read_shape_fast_path_jp2_partial;
  Counter read_shape_fast_path_jp2_fallback;
  Counter read_shape_fast_path_tiff_hit;
  Counter read_shape_fast_path_tiff_miss;
  Counter read_shape_fast_path_tiff_partial;
  Counter read_shape_fast_path_tiff_fallback;

  // Essentials hash-mismatch corruption tripwire (ADR-0010 /
  // DEV-6537). Incremented from:
  //   - `SipiImage::readSource` when the source carries an
  //     Essentials packet and the recomputed pixel hash doesn't
  //     match `data_chksum` (soft signal — log + continue).
  //   - `sipi verify service-file` on the same mismatch
  //     (hard signal — log + non-zero exit).
  // Format = {jp2, tiff, jpeg, png, other}.
  Counter essentials_hash_mismatch_jp2;
  Counter essentials_hash_mismatch_tiff;
  Counter essentials_hash_mismatch_jpeg;
  Counter essentials_hash_mismatch_png;
  Counter essentials_hash_mismatch_other;

  // TIFF decodes that read from a reduced pyramid level (level > 0) instead of
  // the full-resolution IFD. A scalar (no label) so it rides the serve-path
  // snapshot bridge to OTLP.
  Counter tiff_pyramid_reduced_decodes_total;

private:
  Metrics() = default;
};

/*!
 * Outcome labels for the `read_shape_fast_path_*` counters.
 */
enum class ReadShapeFastPathOutcome {
  Hit,
  Miss,
  Partial,
  Fallback,
};

/*!
 * Format labels recognised by the Essentials counters. `Other`
 * exists as a safety valve for any path that funnels into the
 * tripwire from a non-carrier format (which should not happen
 * in practice, but is included for defence-in-depth observability).
 */
enum class EssentialsFormat {
  Jp2,
  Tiff,
  Jpeg,
  Png,
  Other,
};

/*!
 * Map a filename extension to an EssentialsFormat. Used by call sites
 * that only have a filesystem path and need to attribute a metric.
 * Falls back to `Other` for unrecognised extensions.
 */
[[nodiscard]] EssentialsFormat format_from_path(const std::string &path);

/*!
 * Resolve the counter for the (format, outcome) pair.
 */
[[nodiscard]] Counter &read_shape_fast_path_counter(
  EssentialsFormat format,
  ReadShapeFastPathOutcome outcome);

/*!
 * Resolve the counter for the given format.
 */
[[nodiscard]] Counter &essentials_hash_mismatch_counter(EssentialsFormat format);

}// namespace Sipi::observability

#endif// SIPI_OBSERVABILITY_METRICS_H
