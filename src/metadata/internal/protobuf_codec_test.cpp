/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <vector>

#include "metadata/internal/protobuf_codec.h"

namespace {

using Sipi::metadata::internal::CodecParseError;
using Sipi::metadata::internal::decode_essentials;
using Sipi::metadata::internal::encode_essentials;
using Sipi::metadata::internal::EssentialsCodecFields;

EssentialsCodecFields make_fields()
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

TEST(ProtobufCodec, RoundTripPreservesAllFields)
{
  const auto in = make_fields();
  const auto bytes = encode_essentials(in);
  ASSERT_FALSE(bytes.empty());

  const auto parsed = decode_essentials(std::span<const std::byte>(bytes));
  ASSERT_TRUE(parsed.has_value()) << "decode failed: " << static_cast<int>(parsed.error());

  EXPECT_EQ(parsed->format_version, in.format_version);
  EXPECT_EQ(parsed->origname, in.origname);
  EXPECT_EQ(parsed->mimetype, in.mimetype);
  EXPECT_EQ(parsed->hash_type, in.hash_type);
  EXPECT_EQ(parsed->data_chksum, in.data_chksum);
  EXPECT_EQ(parsed->use_icc, in.use_icc);
  EXPECT_EQ(parsed->icc_profile, in.icc_profile);
  EXPECT_EQ(parsed->img_w, in.img_w);
  EXPECT_EQ(parsed->img_h, in.img_h);
  EXPECT_EQ(parsed->tile_w, in.tile_w);
  EXPECT_EQ(parsed->tile_h, in.tile_h);
  EXPECT_EQ(parsed->clevels, in.clevels);
  EXPECT_EQ(parsed->numpages, in.numpages);
  EXPECT_EQ(parsed->nc, in.nc);
  EXPECT_EQ(parsed->bps, in.bps);
}

TEST(ProtobufCodec, EmptyInputReturnsEmpty)
{
  const std::vector<std::byte> empty;
  const auto result = decode_essentials(std::span<const std::byte>(empty));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), CodecParseError::Empty);
}

TEST(ProtobufCodec, MalformedBytesReturnMalformed)
{
  // Random non-protobuf bytes that look like a length-delimited field but
  // claim more data than is actually present.
  const std::vector<std::byte> garbage{ std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF } };
  const auto result = decode_essentials(std::span<const std::byte>(garbage));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), CodecParseError::Malformed);
}

TEST(ProtobufCodec, MissingVersionReturnsMissingVersion)
{
  auto fields = make_fields();
  fields.format_version = 0;// proto3 default — field was never set
  const auto bytes = encode_essentials(fields);
  const auto result = decode_essentials(std::span<const std::byte>(bytes));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), CodecParseError::MissingVersion);
}

TEST(ProtobufCodec, FutureVersionReturnsUnknownVersion)
{
  auto fields = make_fields();
  fields.format_version = 2;// future writer
  const auto bytes = encode_essentials(fields);
  const auto result = decode_essentials(std::span<const std::byte>(bytes));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), CodecParseError::UnknownVersion);
}

TEST(ProtobufCodec, MissingCoreFieldsReturnsMissingCore)
{
  // format_version=1, hash_type=md5, but origname/mimetype/data_chksum empty.
  EssentialsCodecFields fields;
  fields.format_version = 1;
  fields.hash_type = shttps::md5;
  const auto bytes = encode_essentials(fields);
  const auto result = decode_essentials(std::span<const std::byte>(bytes));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), CodecParseError::MissingCore);
}

TEST(ProtobufCodec, EncodeIsByteDeterministic)
{
  // protobuf serializes scalar / length-delimited fields in field-number order,
  // which is stable across protoc revisions for the same `.proto` schema. This
  // is what makes the carrier bytes reproducible for approval tests.
  const auto in = make_fields();
  const auto bytes_a = encode_essentials(in);
  const auto bytes_b = encode_essentials(in);
  EXPECT_EQ(bytes_a, bytes_b);
}

}// namespace
