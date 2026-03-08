/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef __defined_sipi_metrics_h
#define __defined_sipi_metrics_h

#include <memory>
#include <string>

#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>
#include <prometheus/text_serializer.h>

class SipiMetrics
{
  // registry_ must be declared first: C++ initializes members in declaration order
  std::shared_ptr<prometheus::Registry> registry_;

public:
  static SipiMetrics &instance();

  std::shared_ptr<prometheus::Registry> registry() { return registry_; }

  std::string serialize();

  // Counters
  prometheus::Counter &cache_hits_total;
  prometheus::Counter &cache_misses_total;
  prometheus::Counter &cache_evictions_total;
  prometheus::Counter &cache_skips_total;

  // Gauges
  prometheus::Gauge &cache_size_bytes;
  prometheus::Gauge &cache_files;
  prometheus::Gauge &cache_size_limit_bytes;
  prometheus::Gauge &cache_files_limit;

private:
  SipiMetrics();
};

#endif
