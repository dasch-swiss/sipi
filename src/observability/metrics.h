/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_OBSERVABILITY_METRICS_H
#define SIPI_OBSERVABILITY_METRICS_H

#include <memory>
#include <string>

#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>
#include <prometheus/text_serializer.h>

namespace Sipi::observability {

class Metrics
{
  // registry_ must be declared first: C++ initializes members in declaration order
  std::shared_ptr<prometheus::Registry> registry_;

public:
  static Metrics &instance();

  std::shared_ptr<prometheus::Registry> registry() { return registry_; }

  std::string serialize();

  // Counters
  prometheus::Counter &cache_hits_total;
  prometheus::Counter &cache_misses_total;
  prometheus::Counter &cache_evictions_total;
  prometheus::Counter &cache_skips_total;
  prometheus::Counter &image_too_large_total;
  prometheus::Counter &client_disconnected_total;
  prometheus::Counter &memory_alloc_failures_total;

  // Rate limiter counters (R30)
  prometheus::Family<prometheus::Counter> &rate_limit_decisions_total;
  prometheus::Counter &rate_limit_allowed;         ///< cached: decisions_total{action="allowed"}
  prometheus::Counter &rate_limit_rejected;        ///< cached: decisions_total{action="rejected"}
  prometheus::Counter &rate_limit_shadow_rejected; ///< cached: decisions_total{action="shadow_rejected"}
  prometheus::Counter &rate_limit_near_limit_total;

  // Queue counters
  prometheus::Counter &rejected_connections_total;

  // Gauges
  prometheus::Gauge &waiting_connections;
  prometheus::Gauge &cache_size_bytes;
  prometheus::Gauge &cache_files;
  prometheus::Gauge &cache_size_limit_bytes;
  prometheus::Gauge &cache_files_limit;
  prometheus::Gauge &rate_limit_clients_tracked;

  // Memory budget metrics
  prometheus::Gauge &decode_memory_budget_bytes;
  prometheus::Gauge &decode_memory_used_bytes;
  prometheus::Family<prometheus::Counter> &decode_memory_decisions_total;
  prometheus::Counter &decode_memory_acquired;        ///< cached: decisions_total{action="acquired"}
  prometheus::Counter &decode_memory_rejected;        ///< cached: decisions_total{action="rejected"}
  prometheus::Counter &decode_memory_shadow_rejected; ///< cached: decisions_total{action="shadow_rejected"}
  prometheus::Counter &decode_memory_near_limit_total;
  prometheus::Histogram &decode_memory_estimate_bytes;

  // Build info (version correlation)
  prometheus::Gauge &build_info;

  // Histograms
  prometheus::Histogram &request_duration_seconds;

  // read_shape fast path (ADR-0004 / DEV-6537).
  // Labels: format = {jp2, tiff}; outcome = {hit, miss, partial, fallback}.
  //   - hit:      Essentials packet parsed; img_w & img_h populated;
  //               fast path returned shape from packet.
  //   - miss:     No Essentials packet found; slow path computed shape.
  //   - partial:  Essentials parsed but only one of img_w/img_h
  //               non-zero; slow path computed shape.
  //   - fallback: Legacy pipe-delimited carrier present (no shape
  //               fields) OR new-carrier parse error; slow path
  //               computed shape.
  prometheus::Family<prometheus::Counter> &read_shape_fast_path_total;
  prometheus::Counter &read_shape_fast_path_jp2_hit;
  prometheus::Counter &read_shape_fast_path_jp2_miss;
  prometheus::Counter &read_shape_fast_path_jp2_partial;
  prometheus::Counter &read_shape_fast_path_jp2_fallback;
  prometheus::Counter &read_shape_fast_path_tiff_hit;
  prometheus::Counter &read_shape_fast_path_tiff_miss;
  prometheus::Counter &read_shape_fast_path_tiff_partial;
  prometheus::Counter &read_shape_fast_path_tiff_fallback;

  // Essentials hash-mismatch corruption tripwire (ADR-0010 /
  // DEV-6537). Incremented from:
  //   - `SipiImage::readSource` when the source carries an
  //     Essentials packet and the recomputed pixel hash doesn't
  //     match `data_chksum` (soft signal — log + continue).
  //   - `sipi verify service-file` on the same mismatch
  //     (hard signal — log + non-zero exit).
  // Label: format = {jp2, tiff, jpeg, png, other}.
  prometheus::Family<prometheus::Counter> &essentials_hash_mismatch_total;
  prometheus::Counter &essentials_hash_mismatch_jp2;
  prometheus::Counter &essentials_hash_mismatch_tiff;
  prometheus::Counter &essentials_hash_mismatch_jpeg;
  prometheus::Counter &essentials_hash_mismatch_png;
  prometheus::Counter &essentials_hash_mismatch_other;

private:
  Metrics();
};

/*!
 * Outcome labels for `read_shape_fast_path_total`.
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
 * Resolve the pre-created counter child for the (format, outcome) pair.
 * Lives next to the Family so call sites don't pay for label-map lookups
 * on every read.
 */
[[nodiscard]] prometheus::Counter &read_shape_fast_path_counter(
  EssentialsFormat format,
  ReadShapeFastPathOutcome outcome);

/*!
 * Resolve the pre-created counter child for the given format.
 */
[[nodiscard]] prometheus::Counter &essentials_hash_mismatch_counter(EssentialsFormat format);

}// namespace Sipi::observability

#endif// SIPI_OBSERVABILITY_METRICS_H
