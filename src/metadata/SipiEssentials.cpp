/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

#include "metadata/SipiEssentials.h"
#include "shttps/Error.h"

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

std::string base64Encode(const std::vector<unsigned char> &message)
{
  if (message.empty()) return {};

  BIO *bio;
  BIO *b64;
  FILE *stream;

  std::size_t encodedSize = 4 * static_cast<std::size_t>(std::ceil(static_cast<double>(message.size()) / 3));

  char *buffer = static_cast<char *>(std::malloc(encodedSize + 1));
  if (buffer == nullptr) { throw shttps::Error("Failed to allocate memory", errno); }

  stream = fmemopen(buffer, encodedSize + 1, "w");
  b64 = BIO_new(BIO_f_base64());
  bio = BIO_new_fp(stream, BIO_NOCLOSE);
  bio = BIO_push(b64, bio);
  BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(bio, message.data(), static_cast<int>(message.size()));
  (void)BIO_flush(bio);
  BIO_free_all(bio);
  std::fclose(stream);

  std::string res(buffer);
  std::free(buffer);
  return res;
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

std::vector<std::string> split(const std::string &s, const std::string &delimiter)
{
  std::size_t pos_start = 0;
  std::size_t pos_end;
  std::size_t delim_len = delimiter.length();
  std::vector<std::string> res;

  while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
    res.emplace_back(s.substr(pos_start, pos_end - pos_start));
    pos_start = pos_end + delim_len;
  }
  res.emplace_back(s.substr(pos_start));
  return res;
}

std::string hash_type_to_string(shttps::HashType ht)
{
  switch (ht) {
  case shttps::HashType::none:
    return "none";
  case shttps::HashType::md5:
    return "md5";
  case shttps::HashType::sha1:
    return "sha1";
  case shttps::HashType::sha256:
    return "sha256";
  case shttps::HashType::sha384:
    return "sha384";
  case shttps::HashType::sha512:
    return "sha512";
  }
  return "none";
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

}// namespace

namespace Sipi {

SipiEssentials::SipiEssentials(const std::string &serialized)
{
  std::vector<std::string> result = split(serialized, "|");
  if (result.size() < 4) return;// Malformed packet — leave _is_set = false.

  _fields.origname = result[0];
  _fields.mimetype = result[1];
  _fields.hash_type = hash_type_from_string(result[2]);
  _fields.data_chksum = result[3];

  if (result.size() > 5) {
    _fields.use_icc = (result[4] == "USE_ICC");
    if (_fields.use_icc && result[5] != "NULL") { _fields.icc_profile = base64Decode(result[5]); }
  }
  _is_set = true;
}

std::string SipiEssentials::serialize() const
{
  std::string out;
  out.reserve(_fields.origname.size() + _fields.mimetype.size() + _fields.data_chksum.size() + 64);
  out += _fields.origname;
  out += '|';
  out += _fields.mimetype;
  out += '|';
  out += hash_type_to_string(_fields.hash_type);
  out += '|';
  out += _fields.data_chksum;
  out += '|';
  out += _fields.use_icc ? "USE_ICC" : "IGNORE_ICC";
  out += '|';
  if (!_fields.icc_profile.empty()) {
    out += base64Encode(_fields.icc_profile);
  } else {
    out += "NULL";
  }
  return out;
}

}// namespace Sipi
