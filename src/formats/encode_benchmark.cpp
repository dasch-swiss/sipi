/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Encode-tier microbenchmarks — `SipiImage::write()` across SIPI's output
// formats. The source (the @sipi_bench_fixtures 7216×5412 8-bit sRGB
// master) is decoded ONCE on first use, then deep-copied per iteration
// outside the timed region (uniform with the process tier; write() is
// nominally const for a plain RGB source, but format handlers may massage
// the image in place for format constraints, so iterations must not share
// state).
//
// Matrix: JPEG Q75 / Q90, PNG, TIFF, JPEG2000 — the four formats
// `SipiImage::write()` emits (`jpg`/`png`/`tif`/`jpx`, the keys of the
// static SipiIO handler map in SipiImage.cpp). The plan's "WebP" encode
// entry does not exist in SIPI: WebP is supported only as a TIFF-internal
// compression on decode, never as an output format.
//
// Output goes to TEST_TMPDIR (exported by the `just bench` recipe to a
// throwaway mktemp dir); the encode includes the file write, mirroring
// what the server pays on a cache miss. SOURCE_DATE_EPOCH pins the
// wall-clock-stamped ICC creation date in the emitted headers (ADR-0002).
//
// Built only via `just bench encode` (-c opt, manual-tagged cc_binary).
// See docs/src/development/benchmarking.md.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "SipiIO.h"
#include "SipiImage.h"
#include "test_paths.h"

namespace {

std::string fixture(const std::string &name)
{
  const char *dir = std::getenv("SIPI_BENCH_FIXTURES_DIR");
  if (dir == nullptr) {
    std::fprintf(stderr, "SIPI_BENCH_FIXTURES_DIR not set — run via `just bench encode`\n");
    std::exit(1);
  }
  return std::string{ dir } + "/big_building/" + name;
}

// 7216×5412 RGB 8bps, decoded once.
const Sipi::SipiImage &master()
{
  static const Sipi::SipiImage img = [] {
    Sipi::SipiImage i;
    i.read(fixture("flat.tif"));
    return i;
  }();
  return img;
}

int64_t src_bytes()
{
  return static_cast<int64_t>(master().getNx() * master().getNy() * master().getNc() * (master().getBps() / 8));
}

void encode(benchmark::State &state, const char *ftype, const Sipi::SipiCompressionParams *params)
{
  const std::string out = sipi::test::tmp_dir() + "/encode_benchmark_out." + ftype;
  for (auto _ : state) {
    state.PauseTiming();
    Sipi::SipiImage img(master());
    state.ResumeTiming();
    img.write(ftype, out, params);
    benchmark::ClobberMemory();
  }
  std::remove(out.c_str());
  state.SetBytesProcessed(state.iterations() * src_bytes());
}

void BM_EncodeJpegQ75(benchmark::State &state)
{
  const Sipi::SipiCompressionParams params = { { Sipi::JPEG_QUALITY, "75" } };
  encode(state, "jpg", &params);
}
BENCHMARK(BM_EncodeJpegQ75)->Unit(benchmark::kMillisecond);

void BM_EncodeJpegQ90(benchmark::State &state)
{
  const Sipi::SipiCompressionParams params = { { Sipi::JPEG_QUALITY, "90" } };
  encode(state, "jpg", &params);
}
BENCHMARK(BM_EncodeJpegQ90)->Unit(benchmark::kMillisecond);

void BM_EncodePng(benchmark::State &state) { encode(state, "png", nullptr); }
BENCHMARK(BM_EncodePng)->Unit(benchmark::kMillisecond);

void BM_EncodeTiff(benchmark::State &state) { encode(state, "tif", nullptr); }
BENCHMARK(BM_EncodeTiff)->Unit(benchmark::kMillisecond);

void BM_EncodeJ2k(benchmark::State &state) { encode(state, "jpx", nullptr); }
BENCHMARK(BM_EncodeJ2k)->Unit(benchmark::kMillisecond);

}// namespace
