/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Parse-tier microbenchmarks — the IIIF URL component string parsers in
// include/iiifparser/. This is SIPI's per-request front door: every image
// request parses an identifier, region, size, rotation and quality/format
// before any file I/O happens. Pure CPU, no fixtures.
//
// Built only via `just bench` (-c opt, manual-tagged cc_binary); never part
// of `bazel test //...` or coverage. See docs/src/development/benchmarking.md
// and the "no benchmark, no hot-path change" convention in CLAUDE.md.

#include <benchmark/benchmark.h>

#include <string>

#include "iiifparser/SipiIdentifier.h"
#include "iiifparser/SipiQualityFormat.h"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiRotation.h"
#include "iiifparser/SipiSize.h"

namespace {

void BM_ParseIdentifier(benchmark::State &state)
{
  // URL-encoded path + page selector — the costliest identifier shape
  // (percent-decoding + '@page' split).
  const std::string in = "path%2Fto%2Fimage.jp2@3";
  for (auto _ : state) {
    Sipi::SipiIdentifier id(in);
    benchmark::DoNotOptimize(id.getIdentifier().size());
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_ParseIdentifier);

void BM_ParseRegion(benchmark::State &state)
{
  // Percent region — the branch that parses four comma-separated floats.
  const std::string in = "pct:10,10,50,50";
  for (auto _ : state) {
    Sipi::SipiRegion region(in);
    benchmark::DoNotOptimize(region.getType());
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_ParseRegion);

void BM_ParseSize(benchmark::State &state)
{
  // !w,h "best-fit within bounding box" — the MAXDIM branch.
  const std::string in = "!1024,1024";
  for (auto _ : state) {
    Sipi::SipiSize size(in);
    benchmark::DoNotOptimize(size.getType());
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_ParseSize);

void BM_ParseRotation(benchmark::State &state)
{
  // Mirrored fractional rotation — exercises the '!' mirror prefix + float parse.
  const std::string in = "!22.5";
  for (auto _ : state) {
    Sipi::SipiRotation rot(in);
    float angle = 0.0f;
    benchmark::DoNotOptimize(rot.get_rotation(angle));
    benchmark::DoNotOptimize(angle);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_ParseRotation);

void BM_ParseQualityFormat(benchmark::State &state)
{
  const std::string in = "default.jpg";
  for (auto _ : state) {
    Sipi::SipiQualityFormat qf(in);
    benchmark::DoNotOptimize(qf.quality());
    benchmark::DoNotOptimize(qf.format());
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_ParseQualityFormat);

// Full per-request parse: all five components in sequence, mirroring what the
// IIIF handler does on every image request. This is the number that actually
// gates request latency at the parser front door.
void BM_ParseFullRequest(benchmark::State &state)
{
  const std::string ident = "path%2Fto%2Fimage.jp2@3";
  const std::string region = "pct:10,10,50,50";
  const std::string size = "!1024,1024";
  const std::string rotation = "!22.5";
  const std::string qualfmt = "default.jpg";
  for (auto _ : state) {
    Sipi::SipiIdentifier id(ident);
    Sipi::SipiRegion reg(region);
    Sipi::SipiSize sz(size);
    Sipi::SipiRotation rot(rotation);
    Sipi::SipiQualityFormat qf(qualfmt);
    float angle = 0.0f;
    benchmark::DoNotOptimize(id.getIdentifier().size());
    benchmark::DoNotOptimize(reg.getType());
    benchmark::DoNotOptimize(sz.getType());
    benchmark::DoNotOptimize(rot.get_rotation(angle));
    benchmark::DoNotOptimize(qf.quality());
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_ParseFullRequest);

}// namespace
