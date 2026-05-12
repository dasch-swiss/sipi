/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

// Internal helpers for SipiIcc — exposed in a header solely so the unit tests
// in test/unit/sipiicc/ can exercise them with explicit epoch values, side-
// stepping the magic-static cache that drives production reads of
// SOURCE_DATE_EPOCH. No production code outside src/metadata/SipiIcc.cpp
// should include this header.
#ifndef SIPI_METADATA_SIPI_ICC_DETAIL_H
#define SIPI_METADATA_SIPI_ICC_DETAIL_H

#include <cstddef>
#include <ctime>
#include <optional>

namespace Sipi::detail {

// ICC profile header layout (ICC.1:2022 §4.2):
//   bytes 24-35: dateTimeNumber  (six big-endian uInt16 — Y/M/D/h/m/s)
//   bytes 84-99: Profile ID      (16 bytes; MD5 of header-with-zeros)
inline constexpr std::size_t kIccDateTimeOffset = 24;
inline constexpr std::size_t kIccDateTimeLength = 12;
inline constexpr std::size_t kIccProfileIdOffset = 84;
inline constexpr std::size_t kIccProfileIdLength = 16;

// Pure byte-mutation function. When `epoch` has a value, overwrites bytes
// 24-35 (creation date, big-endian Y/M/D/h/m/s) with the gmtime breakdown of
// `*epoch` and zeros bytes 84-99 (Profile ID). When `epoch` is nullopt, the
// buffer is untouched. Buffers shorter than 100 bytes, gmtime_r failures,
// and years outside the ICC dateTimeNumber range (uint16) all bail without
// mutating the buffer. Never throws.
void apply_icc_header_normalization(unsigned char *buf, std::size_t len, std::optional<std::time_t> epoch) noexcept;

}// namespace Sipi::detail

#endif
