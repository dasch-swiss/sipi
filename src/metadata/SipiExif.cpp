/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <climits>
#include <cmath>

#include "SipiError.hpp"
#include "metadata/SipiExif.h"

namespace Sipi {

SipiExif::SipiExif()
{
  binaryExif = nullptr;
  binary_size = 0;
  byteorder = Exiv2::littleEndian;// that's today's default....
};
//============================================================================


SipiExif::SipiExif(const unsigned char *exif, unsigned int len)
{
  //
  // first we save the binary exif... we use it later for constructing a binary exif again!
  //
  binaryExif = new unsigned char[len];
  memcpy(binaryExif, exif, len);
  binary_size = len;

  //
  // now we decode the binary exif
  //
  try {
    byteorder = Exiv2::ExifParser::decode(exifData, exif, (uint32_t)len);
  } catch (Exiv2::Error &exiverr) {
    throw SipiError(exiverr.what());
  }
}
//============================================================================

SipiExif::~SipiExif() { delete[] binaryExif; }

unsigned char *SipiExif::exifBytes(unsigned int &len)
{
  Exiv2::Blob blob;
  Exiv2::WriteMethod wm = Exiv2::ExifParser::encode(blob, binaryExif, binary_size, byteorder, exifData);
  unsigned char *tmpbuf = nullptr;
  if (wm == Exiv2::wmIntrusive) {
    // we use blob
    binary_size = blob.size();
    tmpbuf = new unsigned char[binary_size];
    memcpy(tmpbuf, blob.data(), binary_size);
    delete[] binaryExif;// cleanup tmpbuf!
    binaryExif = tmpbuf;
  }
  len = binary_size;
  return binaryExif;
}
//============================================================================

std::vector<unsigned char> SipiExif::exifBytes()
{
  unsigned int len = 0;
  unsigned char *buf = exifBytes(len);
  std::vector<unsigned char> data(buf, buf + len);
  return data;
}
//============================================================================

Exiv2::Rational SipiExif::toRational(float f)
{
  int numerator = 0;
  int denominator = 0;
  if (f == 0.0F) {
    numerator = 0;
    denominator = 1;
  } else if (f == floorf(f)) {
    numerator = (int)f;
    denominator = 1;
  } else if (f > 0.0F) {
    if (f < 1.0F) {
      numerator = (int)(f * static_cast<float>(LONG_MAX));
      denominator = INT_MAX;
    } else {
      numerator = INT_MAX;
      denominator = (int)(static_cast<float>(INT_MAX) / f);
    }
  } else {
    if (f > -1.0F) {
      numerator = (int)(f * static_cast<float>(INT_MAX));
      denominator = INT_MAX;
    } else {
      numerator = INT_MAX;
      denominator = (int)(static_cast<float>(INT_MAX) / f);
    }
  }
  return std::make_pair(numerator, denominator);
}
//============================================================================

Exiv2::URational SipiExif::toURational(float f)
{
  unsigned int numerator;
  unsigned int denominator;

  if (f < 0.0F) throw SipiError("Cannot convert negative float to URational!");

  if (f == 0.0F) {
    numerator = 0;
    denominator = 1;
  } else if (f == (float)((int)f)) {
    numerator = (int)f;
    denominator = 1;
  }
  if (f < 1.0F) {
    numerator = (int)(f * static_cast<float>(UINT_MAX));
    denominator = UINT_MAX;
  } else {
    numerator = UINT_MAX;
    denominator = (int)(static_cast<float>(UINT_MAX) / f);
  }
  return std::make_pair(numerator, denominator);
}
//============================================================================


std::ostream &operator<<(std::ostream &outstr, SipiExif &rhs)
{
  auto end = rhs.exifData.end();
  for (auto i = rhs.exifData.begin(); i != end; ++i) {
    const char *tn = i->typeName();
    outstr << std::setw(44) << std::setfill(' ') << std::left << i->key() << " "
           << "0x" << std::setw(4) << std::setfill('0') << std::right << std::hex << i->tag() << " " << std::setw(9)
           << std::setfill(' ') << std::left << (tn ? tn : "Unknown") << " " << std::dec << std::setw(3)
           << std::setfill(' ') << std::right << i->count() << "  " << std::dec << i->value() << "\n";
  }
  return outstr;
}
//============================================================================

}// namespace Sipi
