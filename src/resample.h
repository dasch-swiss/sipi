/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_RESAMPLE_H
#define SIPI_RESAMPLE_H

#include <cstddef>
#include <cstdint>

namespace Sipi {

// Fixed-point precision shared by the weight builder (SipiImage.cpp) and the
// SIMD resampler kernel (resample.cc). Weights are integers scaled by
// 2^kResamplePrecisionBits and sum to 2^kResamplePrecisionBits per output
// sample. Integer accumulation is exact and associative, so the kernel produces
// bit-identical output regardless of SIMD lane width or architecture — the
// property that lets it vectorize while a single approval golden set stays valid
// across x86-64 and aarch64. 14 bits keeps 8- and 16-bit sample sums in int32.
inline constexpr int kResamplePrecisionBits = 14;

// Two-pass separable resample of an interleaved image (nx*ny*nc samples) to
// nnx*nny, using CSR-layout fixed-point weights per axis: `off` has dst+1 row
// pointers, `idx`/`wt` have off[dst] entries, and each output's weights sum to
// 2^kResamplePrecisionBits. The horizontal pass (hoff/hidx/hwt) runs first, the
// vertical pass (voff/vidx/vwt) second; `out` receives nnx*nny*nc samples. The
// best SIMD target is selected at runtime via Highway dynamic dispatch.
void resample_separable_u8(const uint8_t *in, size_t nx, size_t ny, size_t nc, size_t nnx, size_t nny,
  const size_t *hoff, const size_t *hidx, const int32_t *hwt, const size_t *voff, const size_t *vidx,
  const int32_t *vwt, uint8_t *out);

void resample_separable_u16(const uint16_t *in, size_t nx, size_t ny, size_t nc, size_t nnx, size_t nny,
  const size_t *hoff, const size_t *hidx, const int32_t *hwt, const size_t *voff, const size_t *vidx,
  const int32_t *vwt, uint16_t *out);

}// namespace Sipi

#endif
