/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

#include "logging/logger.h"
#include "metadata/essentials.h"
#include "metadata/internal/protobuf_codec.h"
#include "shttps/util/Error.h"

namespace {

int calcDecodeLength(const std::string &b64input)
{
  int padding = 0;
  if (b64input.length() >= 2 && b64input[b64input.length() - 1] == '=' && b64input[b64input.length() - 2] == '=')
    padding = 2;
  else if (!b64input.empty() && b64input[b64input.length() - 1] == '=')
    padding = 1;
  return static_cast<int>(b64input.length() * 0.75) - padding;
}

std::vector<unsigned char> base64Decode(const std::string &b64message)
{
  if (b64message.empty()) return {};

  BIO *bio;
  BIO *b64;
  std::size_t decodedLength = calcDecodeLength(b64message);

  auto *buffer = static_cast<unsigned char *>(std::malloc(decodedLength + 1));
  if (buffer == nullptr) { throw shttps::Error("Failed to allocate memory", errno); }
  FILE *stream = fmemopen(const_cast<char *>(b64message.c_str()), b64message.size(), "r");

  b64 = BIO_new(BIO_f_base64());
  bio = BIO_new_fp(stream, BIO_NOCLOSE);
  bio = BIO_push(b64, bio);
  BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
  decodedLength = BIO_read(bio, buffer, static_cast<int>(b64message.size()));
  buffer[decodedLength] = '\0';

  BIO_free_all(bio);
  std::fclose(stream);

  std::vector<unsigned char> data(buffer, buffer + decodedLength);
  std::free(buffer);
  return data;
}

std::vector<std::string> split(std::string_view s, std::string_view delimiter)
{
  std::size_t pos_start = 0;
  std::size_t pos_end;
  std::size_t delim_len = delimiter.length();
  std::vector<std::string> res;

  while ((pos_end = s.find(delimiter, pos_start)) != std::string_view::npos) {
    res.emplace_back(s.substr(pos_start, pos_end - pos_start));
    pos_start = pos_end + delim_len;
  }
  res.emplace_back(s.substr(pos_start));
  return res;
}

shttps::HashType hash_type_from_string(const std::string &s)
{
  if (s == "md5") return shttps::HashType::md5;
  if (s == "sha1") return shttps::HashType::sha1;
  if (s == "sha256") return shttps::HashType::sha256;
  if (s == "sha384") return shttps::HashType::sha384;
  if (s == "sha512") return shttps::HashType::sha512;
  return shttps::HashType::none;
}

Sipi::ParseError adapt_codec_error(Sipi::metadata::internal::CodecParseError e)
{
  using C = Sipi::metadata::internal::CodecParseError;
  using P = Sipi::ParseError;
  switch (e) {
  case C::Empty:
    return P::Empty;
  case C::Malformed:
    return P::Malformed;
  case C::MissingVersion:
    return P::MissingVersion;
  case C::UnknownVersion:
    return P::UnknownVersion;
  case C::MissingCore:
    return P::MissingCore;
  }
  return P::Malformed;
}

}// namespace

namespace Sipi {

Essentials Essentials::parse_legacy(std::string_view serialized)
{
  Essentials out;
  std::vector<std::string> result = split(serialized, "|");
  if (result.size() < 4) return out;// Malformed packet — leave _is_set = false.

  out._fields.origname = result[0];
  out._fields.mimetype = result[1];
  out._fields.hash_type = hash_type_from_string(result[2]);
  out._fields.data_chksum = Essentials::from_hex(result[3]);

  if (result.size() > 5) {
    out._fields.use_icc = (result[4] == "USE_ICC");
    if (out._fields.use_icc && result[5] != "NULL") { out._fields.icc_profile = base64Decode(result[5]); }
  }
  out._is_set = true;
  return out;
}

std::expected<Essentials, ParseError> Essentials::parse(std::span<const std::byte> bytes)
{
  auto codec = metadata::internal::decode_essentials(bytes);
  if (!codec) { return std::unexpected(adapt_codec_error(codec.error())); }

  EssentialsFields f;
  f.origname = std::move(codec->origname);
  f.mimetype = std::move(codec->mimetype);
  f.hash_type = codec->hash_type;
  f.data_chksum = std::move(codec->data_chksum);
  f.use_icc = codec->use_icc;
  f.icc_profile = std::move(codec->icc_profile);
  f.img_w = codec->img_w;
  f.img_h = codec->img_h;
  f.tile_w = codec->tile_w;
  f.tile_h = codec->tile_h;
  f.clevels = codec->clevels;
  f.numpages = codec->numpages;
  f.nc = codec->nc;
  f.bps = codec->bps;

  // Tripwire log: identifies the on-disk format_version each reader sees,
  // which gives us early warning when a future writer ships >1 ahead of a
  // reader migration. Format-handler decide-boundary logs are a separate concern.
  log_info("Essentials: read format_version=%u (max supported: 1)", codec->format_version);

  return Essentials(std::move(f));
}

std::vector<std::byte> Essentials::serialize() const
{
  metadata::internal::EssentialsCodecFields c;
  c.format_version = 1;
  c.origname = _fields.origname;
  c.mimetype = _fields.mimetype;
  c.hash_type = _fields.hash_type;
  c.data_chksum = _fields.data_chksum;
  c.use_icc = _fields.use_icc;
  c.icc_profile = _fields.icc_profile;
  c.img_w = _fields.img_w;
  c.img_h = _fields.img_h;
  c.tile_w = _fields.tile_w;
  c.tile_h = _fields.tile_h;
  c.clevels = _fields.clevels;
  c.numpages = _fields.numpages;
  c.nc = _fields.nc;
  c.bps = _fields.bps;
  return metadata::internal::encode_essentials(c);
}

std::string Essentials::to_hex(std::span<const std::byte> bytes)
{
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (std::byte b : bytes) {
    const auto v = static_cast<unsigned>(b);
    out.push_back(kHex[(v >> 4) & 0xF]);
    out.push_back(kHex[v & 0xF]);
  }
  return out;
}

std::vector<std::byte> Essentials::from_hex(std::string_view hex)
{
  if (hex.size() % 2 != 0) return {};
  std::vector<std::byte> out;
  out.reserve(hex.size() / 2);
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  };
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    int hi = nibble(hex[i]);
    int lo = nibble(hex[i + 1]);
    if (hi < 0 || lo < 0) return {};
    out.push_back(static_cast<std::byte>((hi << 4) | lo));
  }
  return out;
}

}// namespace Sipi
