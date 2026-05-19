/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "Error.h"
#include "Hash.h"
#include "makeunique.h"

using namespace std;

namespace shttps {

Hash::Hash(HashType type)
{
  context = EVP_MD_CTX_create();
  if (context == nullptr) { throw Error("EVP_MD_CTX_create failed!"); }
  int status = 0;
  switch (type) {
  case none: {
    status = EVP_DigestInit_ex(context, EVP_md5(), nullptr);
    break;
  }
  case md5: {
    status = EVP_DigestInit_ex(context, EVP_md5(), nullptr);
    break;
  }
  case sha1: {
    status = EVP_DigestInit_ex(context, EVP_sha1(), nullptr);
    break;
  }
  case sha256: {
    status = EVP_DigestInit_ex(context, EVP_sha256(), nullptr);
    break;
  }
  case sha384: {
    status = EVP_DigestInit_ex(context, EVP_sha384(), nullptr);
    break;
  }
  case sha512: {
    status = EVP_DigestInit_ex(context, EVP_sha512(), nullptr);
    break;
  }
  }
  if (status != 1) {
    EVP_MD_CTX_destroy(context);
    throw Error("EVP_DigestInit_ex failed!");
  }
}
//==========================================================================

Hash::~Hash() { EVP_MD_CTX_destroy(context); }
//==========================================================================

bool Hash::add_data(const void *data, size_t len) { return EVP_DigestUpdate(context, data, len); }
//==========================================================================

bool Hash::hash_of_file(const string &path, size_t buflen)
{
  auto buf = make_unique<char[]>(buflen);

  int fptr = ::open(path.c_str(), O_RDONLY);
  if (fptr == -1) { return false; }
  size_t n;
  while ((n = ::read(fptr, buf.get(), buflen)) > 0) {
    if (n == -1) {
      ::close(fptr);
      return false;
    }
    if (!EVP_DigestUpdate(context, buf.get(), n)) {
      ::close(fptr);
      return false;
    }
  }
  ::close(fptr);
  return true;
}
//==========================================================================

istream &operator>>(istream &input, Hash &h)
{
  char buffer[4096];
  int i = 0;
  while (input.good() && (i < 4096)) { buffer[i++] = input.get(); }
  EVP_DigestUpdate(h.context, buffer, i);
  return input;
}
//==========================================================================

string Hash::hash(void)
{
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int lengthOfHash = 0;
  string hashstr;
  if (EVP_DigestFinal_ex(context, hash, &lengthOfHash)) {
    std::stringstream ss;
    for (unsigned int i = 0; i < lengthOfHash; ++i) {
      ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    hashstr = ss.str();
  }
  return hashstr;
}
//==========================================================================
}// namespace shttps
