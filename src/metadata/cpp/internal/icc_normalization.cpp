/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

#include "icc_normalization.h"

#include <cstdint>
#include <cstring>
#include <ctime>

namespace Sipi::detail {

namespace {

void put_be16(unsigned char *p, std::uint16_t v) noexcept
{
  p[0] = static_cast<unsigned char>(v >> 8);
  p[1] = static_cast<unsigned char>(v & 0xff);
}

}// namespace

// Why zero the Profile ID: lcms2 may round-trip a non-zero ID from an
// externally-authored input profile (Little-CMS issue #181); a non-zero ID
// baked over a scrubbed date is meaningless. Production is unaffected because
// production never sets SOURCE_DATE_EPOCH.
void apply_icc_header_normalization(unsigned char *buf, std::size_t len, std::optional<std::time_t> epoch) noexcept
{
  if (!epoch.has_value()) return;
  if (len < kIccProfileIdOffset + kIccProfileIdLength) return;

  // POSIX gmtime_r returns NULL when the year doesn't fit `int tm_year`
  // (e.g., 32-bit time_t platforms with a post-2038 epoch). A bogus
  // SOURCE_DATE_EPOCH must not silently emit a 1900-stamped profile, so
  // we no-op rather than write garbage. Bail also if the year is outside
  // the ICC dateTimeNumber range (uInt16, [0, 65535]).
  std::tm tm{};
  if (gmtime_r(&*epoch, &tm) == nullptr) return;
  const long year = static_cast<long>(tm.tm_year) + 1900;
  if (year < 0 || year > 65535) return;

  unsigned char *p = buf + kIccDateTimeOffset;
  put_be16(p + 0, static_cast<std::uint16_t>(year));
  put_be16(p + 2, static_cast<std::uint16_t>(tm.tm_mon + 1));
  put_be16(p + 4, static_cast<std::uint16_t>(tm.tm_mday));
  put_be16(p + 6, static_cast<std::uint16_t>(tm.tm_hour));
  put_be16(p + 8, static_cast<std::uint16_t>(tm.tm_min));
  put_be16(p + 10, static_cast<std::uint16_t>(tm.tm_sec));

  std::memset(buf + kIccProfileIdOffset, 0, kIccProfileIdLength);
}

}// namespace Sipi::detail
