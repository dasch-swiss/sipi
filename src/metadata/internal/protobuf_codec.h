/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_METADATA_INTERNAL_PROTOBUF_CODEC_H
#define SIPI_METADATA_INTERNAL_PROTOBUF_CODEC_H

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "shttps/util/Hash.h"

// Internal protobuf codec for the Essentials packet (ADR-0005 / DEV-6410).
//
// This header is the ONLY public surface the metadata module sees onto the
// protobuf machinery. The .cpp is the only translation unit that includes
// `essentials.pb.h`; everything else talks via `EssentialsCodecFields`. This
// keeps the protobuf-generated headers out of SIPI's public ABI and means a
// future `optimize_for` change or `.proto` reshape doesn't ripple through the
// codebase.
//
// DEV-6410 wires the public `Sipi::Essentials::parse` /
// `parse_legacy` factories on top of this codec.

namespace Sipi::metadata::internal {

/// In-memory mirror of the protobuf `Essentials` message. Field-for-field
/// equivalent of the `.proto` schema, expressed in C++ types so the rest of
/// the metadata module never needs to include the generated headers.
struct EssentialsCodecFields
{
  uint32_t format_version = 1;
  std::string origname;
  std::string mimetype;
  shttps::HashType hash_type = shttps::HashType::none;
  std::vector<std::byte> data_chksum;//!< raw digest bytes (NOT hex)
  bool use_icc = false;
  std::vector<unsigned char> icc_profile;
  uint32_t img_w = 0;
  uint32_t img_h = 0;
  uint32_t tile_w = 0;
  uint32_t tile_h = 0;
  uint32_t clevels = 0;
  uint32_t numpages = 0;
  uint32_t nc = 0;
  uint32_t bps = 0;
};

/// Reasons a `decode_essentials` call may fail. The ordering matches the
/// dispatcher in the .cpp: cheaper checks first, then content validation.
enum class CodecParseError {
  Empty,// zero-length input
  Malformed,// protobuf ParseFromArray returned false
  MissingVersion,// format_version == 0 (proto3 default → field never set)
  UnknownVersion,// format_version > 1 (future writer)
  MissingCore,// protobuf parsed but origname/mimetype/hash_type/data_chksum missing
};

/// Encode the fields to the protobuf wire format. Byte-deterministic — protobuf
/// serializes scalar / length-delimited fields in field-number order, which is
/// stable across protoc revisions for the same `.proto` schema.
[[nodiscard]] std::vector<std::byte> encode_essentials(const EssentialsCodecFields &fields);

/// Parse the protobuf wire format. Returns the populated fields on success or
/// a `CodecParseError` on the first failure condition.
[[nodiscard]] std::expected<EssentialsCodecFields, CodecParseError>
  decode_essentials(std::span<const std::byte> bytes);

}// namespace Sipi::metadata::internal

#endif
