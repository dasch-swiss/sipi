/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * SIMD implementation of the fixed-point separable resampler declared in
 * resample.h. The vertical pass accumulates over a contiguous output row, which
 * vectorizes cleanly; the horizontal pass is per-output-column (tap sets differ
 * per column) and stays scalar. Accumulation is int32 fixed-point, so the result
 * is bit-identical to the scalar reference and across every SIMD target — see
 * resample.h and test/unit/sipiimage/scale_resample_test.cpp.
 */

#include "resample.h"

#include <algorithm>
#include <limits>
#include <vector>

// Disable every 512-bit target (all AVX3 variants + AVX10.2): under this
// hermetic-clang toolchain the per-target attribute push does not enable
// `avx512f` for those namespaces, so their intrinsics fail to inline. SSE4 +
// AVX2 cover the x86 fleet (AVX-512 is not universal and often downclocks);
// NEON serves ARM.
#define HWY_DISABLED_TARGETS \
  (HWY_AVX10_2 | HWY_AVX3_SPR | HWY_AVX3_ZEN4 | HWY_AVX3_DL | HWY_AVX3)

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/resample.cc"
#include "hwy/foreach_target.h"// IWYU pragma: keep

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace Sipi {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

// HWY_ATTR is required on the SIMD functions: HWY_BEFORE_NAMESPACE()'s pragma
// attribute-push does not reliably reach template instantiations, so without it
// the per-target features (e.g. avx512f) are dropped from this template's body
// and the intrinsics fail to inline on non-baseline x86 targets.
template<typename T>
HWY_ATTR void ResampleSeparable(const T *in, size_t nx, size_t ny, size_t nc, size_t nnx, size_t nny,
  const size_t *hoff, const size_t *hidx, const int32_t *hwt, const size_t *voff, const size_t *vidx,
  const int32_t *vwt, T *out)
{
  constexpr int32_t maxval = static_cast<int32_t>(std::numeric_limits<T>::max());
  constexpr int32_t round = 1 << (kResamplePrecisionBits - 1);
  const size_t row_len = nnx * nc;

  // Horizontal pass: nx→nnx into an int32 scratch buffer, rounded back into the
  // sample range. Scalar — the tap set varies per output column.
  std::vector<int32_t> tmp(ny * row_len);
  for (size_t y = 0; y < ny; ++y) {
    const T *row = in + y * nx * nc;
    int32_t *orow = tmp.data() + y * row_len;
    for (size_t i = 0; i < nnx; ++i) {
      for (size_t k = 0; k < nc; ++k) {
        int32_t acc = round;
        for (size_t t = hoff[i]; t < hoff[i + 1]; ++t) {
          acc += hwt[t] * static_cast<int32_t>(row[hidx[t] * nc + k]);
        }
        orow[i * nc + k] = std::clamp(acc >> kResamplePrecisionBits, 0, maxval);
      }
    }
  }

  // Vertical pass: ny→nny. Every output column shares the same tap set, so the
  // accumulation is a contiguous int32 AXPY over the whole output row.
  const hn::ScalableTag<int32_t> d;
  const size_t N = hn::Lanes(d);
  const auto vround = hn::Set(d, round);
  // LoadU/StoreU (unaligned): acc and each tmp row (tmp + vidx*row_len) are only
  // int32-aligned, and aligned SSE/AVX load/store fault on misalignment (NEON
  // tolerates it — which is why ARM-only local runs did not catch this).
  std::vector<int32_t> acc(row_len);
  for (size_t j = 0; j < nny; ++j) {
    size_t f = 0;
    for (; f + N <= row_len; f += N) { hn::StoreU(vround, d, acc.data() + f); }
    for (; f < row_len; ++f) { acc[f] = round; }

    for (size_t t = voff[j]; t < voff[j + 1]; ++t) {
      const int32_t w = vwt[t];
      const int32_t *src = tmp.data() + vidx[t] * row_len;
      const auto vw = hn::Set(d, w);
      f = 0;
      for (; f + N <= row_len; f += N) {
        const auto a = hn::LoadU(d, acc.data() + f);
        const auto s = hn::LoadU(d, src + f);
        hn::StoreU(hn::Add(a, hn::Mul(vw, s)), d, acc.data() + f);
      }
      for (; f < row_len; ++f) { acc[f] += w * src[f]; }
    }

    // Round-shift-clamp back to the sample scale. Scalar: it is one cheap pass
    // over the row (not the tap-weighted inner loop) and keeps the kernel's
    // vector surface to the core AXPY that is portable across every target.
    T *orow = out + j * row_len;
    for (f = 0; f < row_len; ++f) {
      orow[f] = static_cast<T>(std::clamp(acc[f] >> kResamplePrecisionBits, 0, maxval));
    }
  }
}

HWY_ATTR void ResampleU8(const uint8_t *in, size_t nx, size_t ny, size_t nc, size_t nnx, size_t nny,
  const size_t *hoff, const size_t *hidx, const int32_t *hwt, const size_t *voff, const size_t *vidx,
  const int32_t *vwt, uint8_t *out)
{
  ResampleSeparable<uint8_t>(in, nx, ny, nc, nnx, nny, hoff, hidx, hwt, voff, vidx, vwt, out);
}

HWY_ATTR void ResampleU16(const uint16_t *in, size_t nx, size_t ny, size_t nc, size_t nnx, size_t nny,
  const size_t *hoff, const size_t *hidx, const int32_t *hwt, const size_t *voff, const size_t *vidx,
  const int32_t *vwt, uint16_t *out)
{
  ResampleSeparable<uint16_t>(in, nx, ny, nc, nnx, nny, hoff, hidx, hwt, voff, vidx, vwt, out);
}

}// namespace HWY_NAMESPACE
}// namespace Sipi
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace Sipi {

HWY_EXPORT(ResampleU8);
HWY_EXPORT(ResampleU16);

void resample_separable_u8(const uint8_t *in, size_t nx, size_t ny, size_t nc, size_t nnx, size_t nny,
  const size_t *hoff, const size_t *hidx, const int32_t *hwt, const size_t *voff, const size_t *vidx,
  const int32_t *vwt, uint8_t *out)
{
  HWY_DYNAMIC_DISPATCH(ResampleU8)
  (in, nx, ny, nc, nnx, nny, hoff, hidx, hwt, voff, vidx, vwt, out);
}

void resample_separable_u16(const uint16_t *in, size_t nx, size_t ny, size_t nc, size_t nnx, size_t nny,
  const size_t *hoff, const size_t *hidx, const int32_t *hwt, const size_t *voff, const size_t *vidx,
  const int32_t *vwt, uint16_t *out)
{
  HWY_DYNAMIC_DISPATCH(ResampleU16)
  (in, nx, ny, nc, nnx, nny, hoff, hidx, hwt, voff, vidx, vwt, out);
}

}// namespace Sipi
#endif
