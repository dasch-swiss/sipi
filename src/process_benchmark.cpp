/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Process-tier microbenchmarks — the image-processing operators between
// decode and encode (scale, rotate, crop, bit-depth reduction, ICC colour
// transform, channel removal). This is the pipeline's middle stage and the
// baseline against which any future SIMD/IPP/GPU acceleration of the
// resampling and colour paths is measured.
//
// Sources are existing small repo fixtures (no benchmark http_archive
// needed); each is decoded ONCE on first use, then deep-copied per
// iteration OUTSIDE the timed region (PauseTiming/ResumeTiming) because
// every operator below mutates the image in place. The pause/resume pair
// costs O(µs) per iteration — negligible against the ms-scale operators
// measured here.
//
// Built only via `just bench` (-c opt, manual-tagged cc_binary); never part
// of `bazel test //...` or coverage. See docs/src/development/benchmarking.md
// and the "no benchmark, no hot-path change" convention in CLAUDE.md.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>

#include "SipiImage.hpp"
#include "metadata/icc.h"
#include "test_paths.hpp"

namespace {

Sipi::SipiImage load(const std::string &rel)
{
  Sipi::SipiImage img;
  img.read(sipi::test::data_dir() + "/images/" + rel);
  return img;
}

// 2591×2572 RGB 8bps — the large known-dims source for the resampling,
// rotation, crop and ICC-throughput operators.
const Sipi::SipiImage &leaves8()
{
  static const Sipi::SipiImage img = load("knora/Leaves8.tif");
  return img;
}

// 864×857 RGBA 8bps — alpha/extra-channel source for removeChannel.
const Sipi::SipiImage &leaves_alpha()
{
  static const Sipi::SipiImage img = load("knora/Leaves-small-alpha.tif");
  return img;
}

// 209×197 RGBA 16bps — 16-bps source for to8bps.
const Sipi::SipiImage &rgba16()
{
  static const Sipi::SipiImage img = load("knora/png_16bit.tif");
  return img;
}

// 128×128 CMYK (Photoshop APP14) — 4-channel source for the CMYK→sRGB
// colour-transform shape.
const Sipi::SipiImage &cmyk128()
{
  static const Sipi::SipiImage img = load("jpeg/cmyk/cmyk_photoshop_app14.jpg");
  return img;
}

// Input-buffer size of the source — throughput is reported relative to the
// bytes the operator reads, not what it writes.
int64_t src_bytes(const Sipi::SipiImage &img)
{
  return static_cast<int64_t>(img.getNx() * img.getNy() * img.getNc() * (img.getBps() / 8));
}

// ── Resampling ──────────────────────────────────────────────────────────
// The three quality levels of the IIIF size stage at the same target dims
// (256 ≈ thumbnail, 1024 ≈ viewer tile) — exactly the bilinear-resampling
// work the Pillay study accelerated via Intel IPP.

void BM_ScaleFast(benchmark::State &state)
{
  const auto dim = static_cast<size_t>(state.range(0));
  for (auto _ : state) {
    state.PauseTiming();
    Sipi::SipiImage img(leaves8());
    state.ResumeTiming();
    img.scaleFast(dim, dim);
    benchmark::DoNotOptimize(img.getNx());
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * src_bytes(leaves8()));
}
BENCHMARK(BM_ScaleFast)->Arg(256)->Arg(1024)->Unit(benchmark::kMillisecond);

void BM_ScaleMedium(benchmark::State &state)
{
  const auto dim = static_cast<size_t>(state.range(0));
  for (auto _ : state) {
    state.PauseTiming();
    Sipi::SipiImage img(leaves8());
    state.ResumeTiming();
    img.scaleMedium(dim, dim);
    benchmark::DoNotOptimize(img.getNx());
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * src_bytes(leaves8()));
}
BENCHMARK(BM_ScaleMedium)->Arg(256)->Arg(1024)->Unit(benchmark::kMillisecond);

void BM_ScaleHigh(benchmark::State &state)
{
  const auto dim = static_cast<size_t>(state.range(0));
  for (auto _ : state) {
    state.PauseTiming();
    Sipi::SipiImage img(leaves8());
    state.ResumeTiming();
    img.scale(dim, dim);
    benchmark::DoNotOptimize(img.getNx());
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * src_bytes(leaves8()));
}
BENCHMARK(BM_ScaleHigh)->Arg(256)->Arg(1024)->Unit(benchmark::kMillisecond);

// ── Rotation ────────────────────────────────────────────────────────────
// 90° takes the special-cased orthogonal path (pixel shuffle); 45° takes
// the general path (bilinear resampling onto a grown canvas).

void BM_Rotate90(benchmark::State &state)
{
  for (auto _ : state) {
    state.PauseTiming();
    Sipi::SipiImage img(leaves8());
    state.ResumeTiming();
    img.rotate(90.0F);
    benchmark::DoNotOptimize(img.getNx());
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * src_bytes(leaves8()));
}
BENCHMARK(BM_Rotate90)->Unit(benchmark::kMillisecond);

void BM_Rotate45(benchmark::State &state)
{
  for (auto _ : state) {
    state.PauseTiming();
    Sipi::SipiImage img(leaves8());
    state.ResumeTiming();
    img.rotate(45.0F);
    benchmark::DoNotOptimize(img.getNx());
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * src_bytes(leaves8()));
}
BENCHMARK(BM_Rotate45)->Unit(benchmark::kMillisecond);

// ── Crop ────────────────────────────────────────────────────────────────
// 1024×1024 region at (512,512) — a typical viewer-tile region extract.

void BM_Crop1024(benchmark::State &state)
{
  for (auto _ : state) {
    state.PauseTiming();
    Sipi::SipiImage img(leaves8());
    state.ResumeTiming();
    img.crop(512, 512, 1024, 1024);
    benchmark::DoNotOptimize(img.getNx());
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * src_bytes(leaves8()));
}
BENCHMARK(BM_Crop1024)->Unit(benchmark::kMillisecond);

// ── Bit-depth reduction ─────────────────────────────────────────────────

void BM_To8bps(benchmark::State &state)
{
  for (auto _ : state) {
    state.PauseTiming();
    Sipi::SipiImage img(rgba16());
    state.ResumeTiming();
    img.to8bps();
    benchmark::DoNotOptimize(img.getBps());
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * src_bytes(rgba16()));
}
BENCHMARK(BM_To8bps);

// ── ICC colour transform ────────────────────────────────────────────────
// Large-RGB throughput (assumed-sRGB → AdobeRGB, 6.7 Mpx) and the
// 4→3-channel CMYK→sRGB shape. Both run littleCMS per-pixel transforms;
// the encode path of every "lossless"-to-lossy conversion pays this cost.

void BM_ConvertToIccAdobeRgb(benchmark::State &state)
{
  for (auto _ : state) {
    state.PauseTiming();
    Sipi::SipiImage img(leaves8());
    state.ResumeTiming();
    img.convertToIcc(Sipi::Icc(Sipi::icc_AdobeRGB), 8);
    benchmark::DoNotOptimize(img.getNc());
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * src_bytes(leaves8()));
}
BENCHMARK(BM_ConvertToIccAdobeRgb)->Unit(benchmark::kMillisecond);

void BM_ConvertToIccCmykToSrgb(benchmark::State &state)
{
  for (auto _ : state) {
    state.PauseTiming();
    Sipi::SipiImage img(cmyk128());
    state.ResumeTiming();
    img.convertToIcc(Sipi::Icc(Sipi::icc_sRGB), 8);
    benchmark::DoNotOptimize(img.getNc());
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * src_bytes(cmyk128()));
}
BENCHMARK(BM_ConvertToIccCmykToSrgb);

// ── Channel removal ─────────────────────────────────────────────────────
// Drop the alpha channel (index 3 of RGBA) — what every JPEG emission of
// an alpha-carrying source does via removeExtraSamples().

void BM_RemoveAlphaChannel(benchmark::State &state)
{
  for (auto _ : state) {
    state.PauseTiming();
    Sipi::SipiImage img(leaves_alpha());
    state.ResumeTiming();
    img.removeChannel(3);
    benchmark::DoNotOptimize(img.getNc());
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * src_bytes(leaves_alpha()));
}
BENCHMARK(BM_RemoveAlphaChannel)->Unit(benchmark::kMillisecond);

}// namespace
