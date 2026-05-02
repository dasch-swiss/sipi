/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "SipiMetrics.h"
#include "generated/SipiVersion.h"

SipiMetrics &SipiMetrics::instance()
{
  static SipiMetrics inst;
  return inst;
}

SipiMetrics::SipiMetrics()
  : registry_(std::make_shared<prometheus::Registry>()),

    cache_hits_total(prometheus::BuildCounter()
                       .Name("sipi_cache_hits_total")
                       .Help("Total number of cache hits")
                       .Register(*registry_)
                       .Add({})),

    cache_misses_total(prometheus::BuildCounter()
                         .Name("sipi_cache_misses_total")
                         .Help("Total number of cache misses")
                         .Register(*registry_)
                         .Add({})),

    cache_evictions_total(prometheus::BuildCounter()
                            .Name("sipi_cache_evictions_total")
                            .Help("Total number of cache evictions")
                            .Register(*registry_)
                            .Add({})),

    cache_skips_total(prometheus::BuildCounter()
                        .Name("sipi_cache_skips_total")
                        .Help("Total number of oversized files skipped")
                        .Register(*registry_)
                        .Add({})),

    image_too_large_total(prometheus::BuildCounter()
                            .Name("sipi_image_too_large_total")
                            .Help("Total requests rejected due to output pixel limit")
                            .Register(*registry_)
                            .Add({})),

    client_disconnected_total(prometheus::BuildCounter()
                                .Name("sipi_client_disconnected_total")
                                .Help("Total requests aborted due to client disconnect during processing")
                                .Register(*registry_)
                                .Add({})),

    memory_alloc_failures_total(prometheus::BuildCounter()
                                  .Name("sipi_memory_alloc_failures_total")
                                  .Help("Total memory allocation failures during image processing")
                                  .Register(*registry_)
                                  .Add({})),

    rate_limit_decisions_total(prometheus::BuildCounter()
                                 .Name("sipi_rate_limit_decisions_total")
                                 .Help("Rate limit decisions by action (allowed, rejected, shadow_rejected)")
                                 .Register(*registry_)),

    rate_limit_near_limit_total(prometheus::BuildCounter()
                                  .Name("sipi_rate_limit_near_limit_total")
                                  .Help("Clients at >80% of pixel budget")
                                  .Register(*registry_)
                                  .Add({})),

    rejected_connections_total(prometheus::BuildCounter()
                                 .Name("sipi_rejected_connections_total")
                                 .Help("Total requests rejected with 503 due to queue full or timeout")
                                 .Register(*registry_)
                                 .Add({})),

    waiting_connections(prometheus::BuildGauge()
                          .Name("sipi_waiting_connections")
                          .Help("Current number of requests in waiting queue")
                          .Register(*registry_)
                          .Add({})),

    cache_size_bytes(prometheus::BuildGauge()
                       .Name("sipi_cache_size_bytes")
                       .Help("Current cache size in bytes")
                       .Register(*registry_)
                       .Add({})),

    cache_files(prometheus::BuildGauge()
                  .Name("sipi_cache_files")
                  .Help("Current number of cached files")
                  .Register(*registry_)
                  .Add({})),

    cache_size_limit_bytes(prometheus::BuildGauge()
                             .Name("sipi_cache_size_limit_bytes")
                             .Help("Configured cache size limit in bytes (-1 = unlimited)")
                             .Register(*registry_)
                             .Add({})),

    cache_files_limit(prometheus::BuildGauge()
                        .Name("sipi_cache_files_limit")
                        .Help("Configured cache file count limit (0 = no limit)")
                        .Register(*registry_)
                        .Add({})),

    rate_limit_clients_tracked(prometheus::BuildGauge()
                                 .Name("sipi_rate_limit_clients_tracked")
                                 .Help("Number of active client entries in rate limiter map")
                                 .Register(*registry_)
                                 .Add({})),

    decode_memory_budget_bytes(prometheus::BuildGauge()
                                  .Name("sipi_decode_memory_budget_bytes")
                                  .Help("Configured decode memory budget in bytes")
                                  .Register(*registry_)
                                  .Add({})),

    decode_memory_used_bytes(prometheus::BuildGauge()
                                .Name("sipi_decode_memory_used_bytes")
                                .Help("Currently allocated to in-flight decodes in bytes")
                                .Register(*registry_)
                                .Add({})),

    decode_memory_decisions_total(prometheus::BuildCounter()
                                    .Name("sipi_decode_memory_decisions_total")
                                    .Help("Memory budget decisions by action (acquired, rejected, shadow_rejected)")
                                    .Register(*registry_)),

    // Pre-create counter children to avoid per-request map lookups
    decode_memory_acquired(decode_memory_decisions_total.Add({{"action", "acquired"}})),
    decode_memory_rejected(decode_memory_decisions_total.Add({{"action", "rejected"}})),
    decode_memory_shadow_rejected(decode_memory_decisions_total.Add({{"action", "shadow_rejected"}})),

    decode_memory_near_limit_total(prometheus::BuildCounter()
                                     .Name("sipi_decode_memory_near_limit_total")
                                     .Help("Acquisitions where usage > 80% of budget (early warning)")
                                     .Register(*registry_)
                                     .Add({})),

    decode_memory_estimate_bytes(
      prometheus::BuildHistogram()
        .Name("sipi_decode_memory_estimate_bytes")
        .Help("Distribution of per-request peak memory estimates in bytes")
        .Register(*registry_)
        .Add({}, prometheus::Histogram::BucketBoundaries{
          1024, 10240, 102400, 1048576, 10485760, 104857600, 524288000, 1073741824, 2147483648.0})),

    build_info(prometheus::BuildGauge()
                  .Name("sipi_build_info")
                  .Help("Sipi build metadata. Value is always 1.")
                  .Register(*registry_)
                  .Add({{"version", BUILD_SCM_TAG}, {"commit", BUILD_SCM_REVISION}})),

    request_duration_seconds(
      prometheus::BuildHistogram()
        .Name("sipi_request_duration_seconds")
        .Help("HTTP request duration in seconds")
        .Register(*registry_)
        .Add({}, prometheus::Histogram::BucketBoundaries{
          0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0}))
{
  // Set build info gauge to 1 (it's an info-style metric — value is always 1, labels carry metadata)
  build_info.Set(1);
}

std::string SipiMetrics::serialize()
{
  prometheus::TextSerializer serializer;
  auto collected = registry_->Collect();
  return serializer.Serialize(collected);
}
