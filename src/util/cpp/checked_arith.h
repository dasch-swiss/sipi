/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * Overflow-checked buffer-size arithmetic for codec decode paths.
 *
 * `checked_buf_size` REJECTS (returns `std::nullopt`) the moment any factor
 * of `nx * ny * nc * elem` overflows `std::size_t`, so a crafted header can
 * never wrap a huge decode buffer size down into a small allocation that a
 * later copy then overruns.
 *
 * This is a deliberately different policy from the saturate-to-`SIZE_MAX`
 * `safe_buf` lambda in `src/throttling/cpp/SipiPeakMemory.h:56-63`: `safe_buf`
 * saturates on purpose so a wrapped memory *estimate* still fails the
 * peak-memory budget comparison it feeds (any comparison against `SIZE_MAX`
 * loses the budget check). `checked_buf_size` instead guards a
 * throw-before-allocate boundary — there is no budget comparison downstream
 * to fail through, so a saturated value would just become the (wrong)
 * allocation size. Reject, don't saturate.
 */
#ifndef SIPI_UTIL_CHECKED_ARITH_H
#define SIPI_UTIL_CHECKED_ARITH_H

#include <cstddef>
#include <optional>

namespace Sipi {

/*!
 * Compute `nx * ny * nc * elem`, checked for overflow at every
 * multiplication.
 *
 * \param nx Width in pixels
 * \param ny Height in pixels
 * \param nc Number of channels
 * \param elem Bytes per sample element
 * \returns The product, or `std::nullopt` if any multiplication overflows.
 */
[[nodiscard]] constexpr std::optional<std::size_t> checked_buf_size(std::size_t nx,
  std::size_t ny,
  std::size_t nc,
  std::size_t elem)
{
  std::size_t product = 0;
  if (__builtin_mul_overflow(nx, ny, &product)) return std::nullopt;
  if (__builtin_mul_overflow(product, nc, &product)) return std::nullopt;
  if (__builtin_mul_overflow(product, elem, &product)) return std::nullopt;
  return product;
}

}// namespace Sipi

#endif// SIPI_UTIL_CHECKED_ARITH_H
