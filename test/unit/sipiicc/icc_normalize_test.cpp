/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

// Unit tests for Sipi::detail::apply_icc_header_normalization — the pure
// byte-mutation function that backs SipiIcc::iccBytes()'s SOURCE_DATE_EPOCH
// reproducibility hook. Tests pass an explicit epoch directly so they don't
// have to fight with the production magic-static cache that backs
// read_source_date_epoch().

#include "metadata/SipiIccDetail.h"

#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// Build a synthetic ICC profile buffer of the given size, filled with 0xAA so
// any spot we don't intentionally write is visibly distinct from a zero.
std::vector<unsigned char> make_synthetic_icc(std::size_t size, unsigned char fill = 0xAA)
{
  return std::vector<unsigned char>(size, fill);
}

constexpr std::time_t kY2k = 946684800;// 2000-01-01T00:00:00Z UTC

// Expected creation-date bytes for kY2k under the ICC.1:2022 §4.2 layout:
// six big-endian uInt16 fields — year, month, day, hour, minute, second.
constexpr std::array<unsigned char, 12> kY2kDateBytes = {
  0x07, 0xD0,// 2000
  0x00, 0x01,// 01
  0x00, 0x01,// 01
  0x00, 0x00,// 00
  0x00, 0x00,// 00
  0x00, 0x00,// 00
};

}// namespace

// AC: SOURCE_DATE_EPOCH unset → buffer unchanged.
TEST(SipiIccNormalize, NoEpochLeavesBufferUnchanged)
{
  auto buf = make_synthetic_icc(256);
  auto original = buf;

  Sipi::detail::apply_icc_header_normalization(buf.data(), buf.size(), std::nullopt);

  EXPECT_EQ(buf, original);
}

// AC: SOURCE_DATE_EPOCH=946684800 → bytes 24-35 = 07D0 0001 0001 0000 0000 0000.
TEST(SipiIccNormalize, EpochY2kWritesExpectedDateBytes)
{
  auto buf = make_synthetic_icc(256);

  Sipi::detail::apply_icc_header_normalization(buf.data(), buf.size(), kY2k);

  for (std::size_t i = 0; i < kY2kDateBytes.size(); ++i) {
    EXPECT_EQ(buf[Sipi::detail::kIccDateTimeOffset + i], kY2kDateBytes[i])
      << "mismatch at date byte " << i;
  }
}

// AC: SOURCE_DATE_EPOCH=946684800 → bytes 84-99 zeroed.
TEST(SipiIccNormalize, EpochY2kZerosProfileId)
{
  auto buf = make_synthetic_icc(256);

  Sipi::detail::apply_icc_header_normalization(buf.data(), buf.size(), kY2k);

  for (std::size_t i = 0; i < Sipi::detail::kIccProfileIdLength; ++i) {
    EXPECT_EQ(buf[Sipi::detail::kIccProfileIdOffset + i], 0u) << "non-zero ID byte at " << i;
  }
}

// AC: non-zero Profile ID input → ID zeroed when flag is set (no throw).
TEST(SipiIccNormalize, NonZeroProfileIdIsScrubbed)
{
  auto buf = make_synthetic_icc(256);
  // Plant a recognizable non-zero ID (this is what lcms2 might leave behind
  // after round-tripping an externally-authored profile — see Little-CMS #181).
  for (std::size_t i = 0; i < Sipi::detail::kIccProfileIdLength; ++i) {
    buf[Sipi::detail::kIccProfileIdOffset + i] = static_cast<unsigned char>(0xC0 + i);
  }

  Sipi::detail::apply_icc_header_normalization(buf.data(), buf.size(), kY2k);

  for (std::size_t i = 0; i < Sipi::detail::kIccProfileIdLength; ++i) {
    EXPECT_EQ(buf[Sipi::detail::kIccProfileIdOffset + i], 0u);
  }
}

// AC: truncated buffer → no-op (no throw, no crash).
TEST(SipiIccNormalize, TruncatedBufferIsNoOp)
{
  // Buffer shorter than offset+length of the Profile ID field — anything in
  // the date region exists, but we still bail to avoid an out-of-bounds memset.
  auto buf = make_synthetic_icc(50);
  auto original = buf;

  Sipi::detail::apply_icc_header_normalization(buf.data(), buf.size(), kY2k);

  EXPECT_EQ(buf, original);
}

TEST(SipiIccNormalize, EmptyBufferIsNoOp)
{
  std::vector<unsigned char> buf;
  EXPECT_NO_THROW(Sipi::detail::apply_icc_header_normalization(buf.data(), buf.size(), kY2k));
}

// AC: idempotent (helper run twice produces identical bytes).
TEST(SipiIccNormalize, IsIdempotent)
{
  auto buf = make_synthetic_icc(256);

  Sipi::detail::apply_icc_header_normalization(buf.data(), buf.size(), kY2k);
  auto first = buf;
  Sipi::detail::apply_icc_header_normalization(buf.data(), buf.size(), kY2k);

  EXPECT_EQ(buf, first);
}

// AC (covered indirectly): malformed SOURCE_DATE_EPOCH → no-op. The env
// parser in read_source_date_epoch() returns nullopt for malformed input,
// which routes through the apply function as the "no-op" branch tested here.
// We don't separately test the parser because its result is cached via
// magic-static and would couple every test to one process-wide env state.
TEST(SipiIccNormalize, NulloptEpochMatchesMalformedEnvBehaviour)
{
  auto buf = make_synthetic_icc(200);
  auto original = buf;

  Sipi::detail::apply_icc_header_normalization(buf.data(), buf.size(), std::nullopt);

  EXPECT_EQ(buf, original);
}

// AC: gmtime_r returning nullptr (e.g., 32-bit time_t + post-2038 epoch) ->
// no-op. We can't easily induce a real gmtime_r failure on a 64-bit time_t
// platform, but year-out-of-range is the same code path and is exposed as
// a public-ish contract via the bounds check. Use a far-future epoch; on
// 64-bit time_t platforms gmtime_r succeeds and returns a year > 65535,
// which the bounds check rejects -> buffer unchanged.
TEST(SipiIccNormalize, EpochProducingOutOfRangeYearIsNoOp)
{
  auto buf = make_synthetic_icc(256);
  auto original = buf;

  // ~year 1 million in seconds-since-epoch. Well above ICC dateTimeNumber's
  // uint16 ceiling. On 32-bit time_t this overflows and gmtime_r returns
  // nullptr; on 64-bit time_t we land at a year > 65535. Both paths bail.
  constexpr std::time_t kHugeEpoch = static_cast<std::time_t>(31556926000000LL);
  Sipi::detail::apply_icc_header_normalization(buf.data(), buf.size(), kHugeEpoch);

  EXPECT_EQ(buf, original);
}

// Sanity: only the documented byte ranges are touched.
TEST(SipiIccNormalize, OnlyDateAndProfileIdRangesAreMutated)
{
  auto buf = make_synthetic_icc(256, 0xAA);

  Sipi::detail::apply_icc_header_normalization(buf.data(), buf.size(), kY2k);

  // Every byte before the date field is untouched.
  for (std::size_t i = 0; i < Sipi::detail::kIccDateTimeOffset; ++i) {
    EXPECT_EQ(buf[i], 0xAAu) << "byte " << i << " before date field was touched";
  }
  // Bytes between the date field and the Profile ID are untouched.
  for (std::size_t i = Sipi::detail::kIccDateTimeOffset + Sipi::detail::kIccDateTimeLength;
       i < Sipi::detail::kIccProfileIdOffset;
       ++i) {
    EXPECT_EQ(buf[i], 0xAAu) << "byte " << i << " between fields was touched";
  }
  // Every byte after the Profile ID is untouched.
  for (std::size_t i = Sipi::detail::kIccProfileIdOffset + Sipi::detail::kIccProfileIdLength;
       i < buf.size();
       ++i) {
    EXPECT_EQ(buf[i], 0xAAu) << "byte " << i << " after Profile ID was touched";
  }
}
