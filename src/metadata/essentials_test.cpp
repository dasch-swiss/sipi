/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Unit tests for the public `Sipi::Essentials` factory surface (Phase 5 /
// DEV-6410). Two concerns covered here:
//
//   1. `parse(span)` — public protobuf entry point: adapter from
//      `internal::CodecParseError` to `Sipi::ParseError` must lift each of
//      the 5 codec failure modes to the matching public variant, and the
//      success path must populate every `EssentialsFields` member.
//
//   2. `parse_legacy(string_view)` — equivalence against a frozen
//      pipe-delimited baseline. Hex `data_chksum` must round-trip to
//      raw bytes via `from_hex` / `to_hex` so the post-Phase-5 type flip
//      doesn't break legacy reads (the legacy reader path is retained
//      indefinitely per DEV-6398 scope).

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "metadata/essentials.h"
#include "metadata/internal/protobuf_codec.h"

namespace {

using Sipi::Essentials;
using Sipi::EssentialsFields;
using Sipi::ParseError;
using Sipi::metadata::internal::EssentialsCodecFields;
using Sipi::metadata::internal::encode_essentials;

// A populated codec-fields fixture that mirrors the protobuf_codec_test fixture
// 1:1, so equivalence with the round-trip there is by construction.
EssentialsCodecFields make_codec_fields()
{
  EssentialsCodecFields f;
  f.format_version = 1;
  f.origname = "mario.png";
  f.mimetype = "image/png";
  f.hash_type = shttps::sha256;
  f.data_chksum = std::vector<std::byte>{ std::byte{ 0x01 }, std::byte{ 0x23 }, std::byte{ 0x45 }, std::byte{ 0x67 } };
  f.use_icc = true;
  f.icc_profile = std::vector<unsigned char>{ 0xDE, 0xAD, 0xBE, 0xEF };
  f.img_w = 1024;
  f.img_h = 768;
  f.tile_w = 256;
  f.tile_h = 256;
  f.clevels = 5;
  f.numpages = 1;
  f.nc = 3;
  f.bps = 8;
  return f;
}

// --- parse(span) success path ----------------------------------------------

TEST(EssentialsParse, RoundTripPopulatesAllFields)
{
  // Encode via the internal codec (the only way to author the wire form from
  // tests; production callers will use Essentials::serialize_bytes()).
  const auto bytes = encode_essentials(make_codec_fields());
  ASSERT_FALSE(bytes.empty());

  const auto result = Essentials::parse(std::span<const std::byte>(bytes));
  ASSERT_TRUE(result.has_value()) << "parse failed: " << static_cast<int>(result.error());
  ASSERT_TRUE(result->is_set());

  const auto &f = result->fields();
  EXPECT_EQ(f.origname, "mario.png");
  EXPECT_EQ(f.mimetype, "image/png");
  EXPECT_EQ(f.hash_type, shttps::sha256);
  EXPECT_EQ(f.data_chksum,
    (std::vector<std::byte>{ std::byte{ 0x01 }, std::byte{ 0x23 }, std::byte{ 0x45 }, std::byte{ 0x67 } }));
  EXPECT_TRUE(f.use_icc);
  EXPECT_EQ(f.icc_profile, (std::vector<unsigned char>{ 0xDE, 0xAD, 0xBE, 0xEF }));
  EXPECT_EQ(f.img_w, 1024u);
  EXPECT_EQ(f.img_h, 768u);
  EXPECT_EQ(f.tile_w, 256u);
  EXPECT_EQ(f.tile_h, 256u);
  EXPECT_EQ(f.clevels, 5u);
  EXPECT_EQ(f.numpages, 1u);
  EXPECT_EQ(f.nc, 3u);
  EXPECT_EQ(f.bps, 8u);
}

TEST(EssentialsParse, SerializeBytesRoundTripsThroughParse)
{
  EssentialsFields in;
  in.origname = "roundtrip.tif";
  in.mimetype = "image/tiff";
  in.hash_type = shttps::sha512;
  in.data_chksum = std::vector<std::byte>{ std::byte{ 0xAB }, std::byte{ 0xCD }, std::byte{ 0xEF } };
  in.use_icc = false;
  in.img_w = 4096;
  in.img_h = 4096;
  in.tile_w = 512;
  in.tile_h = 512;
  in.clevels = 6;
  in.numpages = 1;
  in.nc = 1;
  in.bps = 16;

  const Essentials a(std::move(in));
  const auto bytes = a.serialize_bytes();
  ASSERT_FALSE(bytes.empty());

  const auto result = Essentials::parse(std::span<const std::byte>(bytes));
  ASSERT_TRUE(result.has_value());

  EXPECT_EQ(result->fields().origname, a.fields().origname);
  EXPECT_EQ(result->fields().mimetype, a.fields().mimetype);
  EXPECT_EQ(result->fields().hash_type, a.fields().hash_type);
  EXPECT_EQ(result->fields().data_chksum, a.fields().data_chksum);
  EXPECT_EQ(result->fields().img_w, a.fields().img_w);
  EXPECT_EQ(result->fields().bps, a.fields().bps);
}

// --- parse(span) failure paths: 5 ParseError variants ----------------------

TEST(EssentialsParse, EmptyInputReturnsEmpty)
{
  const std::vector<std::byte> empty;
  const auto result = Essentials::parse(std::span<const std::byte>(empty));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ParseError::Empty);
}

TEST(EssentialsParse, MalformedBytesReturnsMalformed)
{
  const std::vector<std::byte> garbage{ std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF } };
  const auto result = Essentials::parse(std::span<const std::byte>(garbage));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ParseError::Malformed);
}

TEST(EssentialsParse, MissingVersionReturnsMissingVersion)
{
  auto fields = make_codec_fields();
  fields.format_version = 0;// proto3 default — field was never set
  const auto bytes = encode_essentials(fields);
  const auto result = Essentials::parse(std::span<const std::byte>(bytes));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ParseError::MissingVersion);
}

TEST(EssentialsParse, FutureVersionReturnsUnknownVersion)
{
  auto fields = make_codec_fields();
  fields.format_version = 2;// future writer
  const auto bytes = encode_essentials(fields);
  const auto result = Essentials::parse(std::span<const std::byte>(bytes));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ParseError::UnknownVersion);
}

TEST(EssentialsParse, MissingCoreFieldsReturnsMissingCore)
{
  // format_version=1, hash_type=md5, but origname/mimetype/data_chksum empty —
  // protobuf accepts the wire bytes but our reader rejects them.
  EssentialsCodecFields fields;
  fields.format_version = 1;
  fields.hash_type = shttps::md5;
  const auto bytes = encode_essentials(fields);
  const auto result = Essentials::parse(std::span<const std::byte>(bytes));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ParseError::MissingCore);
}

// --- parse_legacy(string_view) ---------------------------------------------

// The frozen pipe-delimited baseline. Pre-Phase 5 emissions look exactly like
// this (the legacy `serialize()` body, unchanged). The hex `data_chksum`
// "0123456789abcdef" decodes to bytes {0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
// 0xcd, 0xef}. Hex case is preserved on the input side; both lower and upper
// are accepted on `from_hex`.
constexpr std::string_view kLegacyBaseline = "scan.tif|image/tiff|sha256|0123456789abcdef|IGNORE_ICC|NULL";

TEST(EssentialsParseLegacy, FrozenBaselinePopulatesEveryField)
{
  const auto e = Essentials::parse_legacy(kLegacyBaseline);
  ASSERT_TRUE(e.is_set());
  EXPECT_EQ(e.fields().origname, "scan.tif");
  EXPECT_EQ(e.fields().mimetype, "image/tiff");
  EXPECT_EQ(e.fields().hash_type, shttps::sha256);
  EXPECT_EQ(e.fields().data_chksum,
    (std::vector<std::byte>{ std::byte{ 0x01 }, std::byte{ 0x23 }, std::byte{ 0x45 }, std::byte{ 0x67 }, std::byte{ 0x89 },
      std::byte{ 0xAB }, std::byte{ 0xCD }, std::byte{ 0xEF } }));
  EXPECT_FALSE(e.fields().use_icc);
  EXPECT_TRUE(e.fields().icc_profile.empty());
}

TEST(EssentialsParseLegacy, MalformedReturnsUnsetPacket)
{
  // Fewer than 4 pipe-delimited tokens — the legacy reader silently produces
  // an unset packet. Same behaviour as the pre-Phase-5 ctor.
  const auto e = Essentials::parse_legacy("only|three|fields");
  EXPECT_FALSE(e.is_set());
}

TEST(EssentialsParseLegacy, LegacyCtorIsAliasForParseLegacy)
{
  // The deprecated `Essentials(const std::string&)` ctor forwards to
  // `parse_legacy`. Removing the ctor in Phase 14 must not change observed
  // behaviour for the legacy reader sites in Phase 6.
  const std::string s(kLegacyBaseline);
  const Essentials via_ctor(s);
  const Essentials via_factory = Essentials::parse_legacy(s);
  EXPECT_EQ(via_ctor.is_set(), via_factory.is_set());
  EXPECT_EQ(via_ctor.fields().origname, via_factory.fields().origname);
  EXPECT_EQ(via_ctor.fields().data_chksum, via_factory.fields().data_chksum);
}

// --- to_hex / from_hex -----------------------------------------------------

TEST(EssentialsHex, RoundTrip)
{
  const std::vector<std::byte> bytes{
    std::byte{ 0x00 }, std::byte{ 0x01 }, std::byte{ 0x7F }, std::byte{ 0x80 }, std::byte{ 0xFE }, std::byte{ 0xFF }
  };
  const std::string hex = Essentials::to_hex(bytes);
  EXPECT_EQ(hex, "00017f80feff");
  EXPECT_EQ(Essentials::from_hex(hex), bytes);
}

TEST(EssentialsHex, FromHexAcceptsMixedCase)
{
  const auto lower = Essentials::from_hex("abcdef");
  const auto upper = Essentials::from_hex("ABCDEF");
  EXPECT_EQ(lower, upper);
}

TEST(EssentialsHex, FromHexRejectsOddLengthAndNonHex)
{
  EXPECT_TRUE(Essentials::from_hex("abc").empty());// odd length
  EXPECT_TRUE(Essentials::from_hex("zz").empty());// non-hex digit
}

}// namespace
