/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "SipiMetrics.h"

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
                        .Add({}))
{
}

std::string SipiMetrics::serialize()
{
  prometheus::TextSerializer serializer;
  auto collected = registry_->Collect();
  return serializer.Serialize(collected);
}
