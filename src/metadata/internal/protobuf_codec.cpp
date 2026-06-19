/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "metadata/internal/protobuf_codec.h"

#include <cstring>
#include <string>

// The ONLY translation unit that includes the generated protobuf headers.
// Every other consumer talks via `EssentialsCodecFields` (ADR-0005 / DEV-6410).
#include "src/metadata/essentials.pb.h"

namespace Sipi::metadata::internal {

namespace {

// Cross-context coupling lock: SIPI's on-disk `data_chksum` is decoded by the
// `hash_type` field, whose integer value is the same on both sides of the
// codec. If a future change reorders `shttps::HashType`, these static_asserts
// fire at build time — long before the first failed file read in production.
// See `shttps/util/Hash.h` for the reverse-pointer comment.
static_assert(static_cast<int>(shttps::none) == sipi::metadata::HASH_TYPE_UNSPECIFIED,
  "shttps::HashType::none integer mapping changed; update essentials.proto in lockstep");
static_assert(static_cast<int>(shttps::md5) == sipi::metadata::HASH_TYPE_MD5,
  "shttps::HashType::md5 integer mapping changed; update essentials.proto in lockstep");
static_assert(static_cast<int>(shttps::sha1) == sipi::metadata::HASH_TYPE_SHA1,
  "shttps::HashType::sha1 integer mapping changed; update essentials.proto in lockstep");
static_assert(static_cast<int>(shttps::sha256) == sipi::metadata::HASH_TYPE_SHA256,
  "shttps::HashType::sha256 integer mapping changed; update essentials.proto in lockstep");
static_assert(static_cast<int>(shttps::sha384) == sipi::metadata::HASH_TYPE_SHA384,
  "shttps::HashType::sha384 integer mapping changed; update essentials.proto in lockstep");
static_assert(static_cast<int>(shttps::sha512) == sipi::metadata::HASH_TYPE_SHA512,
  "shttps::HashType::sha512 integer mapping changed; update essentials.proto in lockstep");

}// namespace

std::vector<std::byte> encode_essentials(const EssentialsCodecFields &fields)
{
  sipi::metadata::Essentials msg;
  msg.set_format_version(fields.format_version);
  msg.set_origname(fields.origname);
  msg.set_mimetype(fields.mimetype);
  msg.set_hash_type(static_cast<sipi::metadata::HashType>(fields.hash_type));
  msg.set_data_chksum(fields.data_chksum.data(), fields.data_chksum.size());
  msg.set_use_icc(fields.use_icc);
  msg.set_icc_profile(fields.icc_profile.data(), fields.icc_profile.size());
  msg.set_img_w(fields.img_w);
  msg.set_img_h(fields.img_h);
  msg.set_tile_w(fields.tile_w);
  msg.set_tile_h(fields.tile_h);
  msg.set_clevels(fields.clevels);
  msg.set_numpages(fields.numpages);
  msg.set_nc(fields.nc);
  msg.set_bps(fields.bps);

  std::string serialized;
  msg.SerializeToString(&serialized);

  std::vector<std::byte> out(serialized.size());
  if (!serialized.empty()) { std::memcpy(out.data(), serialized.data(), serialized.size()); }
  return out;
}

std::expected<EssentialsCodecFields, CodecParseError>
  decode_essentials(std::span<const std::byte> bytes)
{
  if (bytes.empty()) { return std::unexpected(CodecParseError::Empty); }

  sipi::metadata::Essentials msg;
  if (!msg.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) {
    return std::unexpected(CodecParseError::Malformed);
  }

  if (msg.format_version() == 0) { return std::unexpected(CodecParseError::MissingVersion); }
  if (msg.format_version() > 1) { return std::unexpected(CodecParseError::UnknownVersion); }

  if (msg.origname().empty() || msg.mimetype().empty()
      || msg.hash_type() == sipi::metadata::HASH_TYPE_UNSPECIFIED || msg.data_chksum().empty()) {
    return std::unexpected(CodecParseError::MissingCore);
  }

  EssentialsCodecFields out;
  out.format_version = msg.format_version();
  out.origname = msg.origname();
  out.mimetype = msg.mimetype();
  out.hash_type = static_cast<shttps::HashType>(msg.hash_type());

  const auto &chksum = msg.data_chksum();
  out.data_chksum.resize(chksum.size());
  if (!chksum.empty()) { std::memcpy(out.data_chksum.data(), chksum.data(), chksum.size()); }

  out.use_icc = msg.use_icc();

  const auto &icc = msg.icc_profile();
  out.icc_profile.assign(icc.begin(), icc.end());

  out.img_w = msg.img_w();
  out.img_h = msg.img_h();
  out.tile_w = msg.tile_w();
  out.tile_h = msg.tile_h();
  out.clevels = msg.clevels();
  out.numpages = msg.numpages();
  out.nc = msg.nc();
  out.bps = msg.bps();

  return out;
}

}// namespace Sipi::metadata::internal
