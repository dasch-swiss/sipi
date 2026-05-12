/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <climits>
#include <cmath>
#include <cstring>
#include <memory>

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
  // Hold the buffer in a unique_ptr until decode succeeds. If
  // Exiv2::ExifParser::decode throws (e.g. on malformed EXIF embedded
  // in PNG text comments), the unique_ptr cleans up on stack unwind.
  // Releasing into the raw member only after decode succeeds keeps
  // the destructor's `delete[]` honest.
  auto buf = std::make_unique<unsigned char[]>(len);
  std::memcpy(buf.get(), exif, len);

  try {
    byteorder = Exiv2::ExifParser::decode(exifData, exif, (uint32_t)len);
  } catch (Exiv2::Error &exiverr) {
    throw SipiError(exiverr.what());
  }

  binaryExif = buf.release();
  binary_size = len;
}
//============================================================================

SipiExif::~SipiExif() { delete[] binaryExif; }
//============================================================================

// Type-dispatched assignment helpers backing the inline `getValByKey<T>`
// templates in the header. Moved out-of-line per DEV-6407 to keep
// `metadata/SipiExif.h` lean.

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, std::string &val)
{
  val = v->toString();
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, std::vector<std::string> &val)
{
  for (int i = 0; i < v->count(); i++) { val.push_back(v->toString(i)); }
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, char &val)
{
  val = static_cast<char>(v->toInt64());
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, std::vector<char> &val)
{
  for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<char>(v->toInt64(i))); }
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, unsigned char &val)
{
  val = static_cast<unsigned char>(v->toInt64());
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, std::vector<unsigned char> &val)
{
  for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<unsigned char>(v->toInt64(i))); }
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, short &val)
{
  val = static_cast<short>(v->toInt64());
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, std::vector<short> &val)
{
  for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<short>(v->toInt64(i))); }
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, unsigned short &val)
{
  val = static_cast<unsigned short>(v->toInt64());
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, std::vector<unsigned short> &val)
{
  for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<unsigned short>(v->toInt64(i))); }
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, int &val)
{
  val = static_cast<int>(v->toInt64());
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, std::vector<int> &val)
{
  for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<int>(v->toInt64(i))); }
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, unsigned int &val)
{
  val = static_cast<unsigned int>(v->toInt64());
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, std::vector<unsigned int> &val)
{
  for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<unsigned int>(v->toInt64(i))); }
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, float &val)
{
  val = static_cast<float>(v->toFloat());
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, std::vector<float> &val)
{
  for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<float>(v->toFloat(i))); }
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, double &val)
{
  val = static_cast<double>(v->toFloat());
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, std::vector<double> &val)
{
  for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<double>(v->toFloat(i))); }
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, Exiv2::Rational &val)
{
  val = v->toRational();
  return v->ok();
}

bool SipiExif::assign_val(Exiv2::Value::UniquePtr &v, std::vector<Exiv2::Rational> &val)
{
  for (int i = 0; i < v->count(); i++) { val.push_back(v->toRational(i)); }
  return v->ok();
}

std::vector<unsigned char> SipiExif::exifBytes()
{
  Exiv2::Blob blob;
  Exiv2::WriteMethod wm = Exiv2::ExifParser::encode(blob, binaryExif, binary_size, byteorder, exifData);
  if (wm == Exiv2::wmIntrusive) {
    // Refresh the cached binary blob so subsequent reads see the encoded form.
    binary_size = blob.size();
    unsigned char *tmpbuf = new unsigned char[binary_size];
    if (binary_size > 0) memcpy(tmpbuf, blob.data(), binary_size);
    delete[] binaryExif;
    binaryExif = tmpbuf;
  }
  return std::vector<unsigned char>(binaryExif, binaryExif + binary_size);
}
//============================================================================

Exiv2::Rational SipiExif::toRational(float f)
{
  if (!std::isfinite(f)) { return std::make_pair(0, 1); }

  const double value = static_cast<double>(f);
  if (value == 0.0) { return std::make_pair(0, 1); }
  if (std::trunc(value) == value && value >= static_cast<double>(INT_MIN) && value <= static_cast<double>(INT_MAX)) {
    return std::make_pair(static_cast<int>(value), 1);
  }

  int denominator = INT_MAX;
  double scaled = value * static_cast<double>(denominator);
  while ((scaled > static_cast<double>(INT_MAX) || scaled < static_cast<double>(INT_MIN)) && denominator > 1) {
    denominator /= 10;
    scaled = value * static_cast<double>(denominator);
  }

  if (denominator < 1) { denominator = 1; }

  if (scaled > static_cast<double>(INT_MAX)) { scaled = static_cast<double>(INT_MAX); }
  if (scaled < static_cast<double>(INT_MIN)) { scaled = static_cast<double>(INT_MIN); }

  int numerator = static_cast<int>(std::round(scaled));
  if (numerator == 0) { numerator = (value > 0.0) ? 1 : -1; }

  return std::make_pair(numerator, denominator);
}
//============================================================================

Exiv2::URational SipiExif::toURational(float f)
{
  if (!std::isfinite(f)) throw SipiError("Cannot convert non-finite float to URational!");
  if (f < 0.0F) throw SipiError("Cannot convert negative float to URational!");

  const double value = static_cast<double>(f);
  if (value == 0.0) { return std::make_pair(0U, 1U); }
  if (std::trunc(value) == value && value <= static_cast<double>(UINT_MAX)) {
    return std::make_pair(static_cast<unsigned int>(value), 1U);
  }

  unsigned int denominator = UINT_MAX;
  double scaled = value * static_cast<double>(denominator);
  while (scaled > static_cast<double>(UINT_MAX) && denominator > 1U) {
    denominator /= 10U;
    scaled = value * static_cast<double>(denominator);
  }

  if (denominator < 1U) { denominator = 1U; }
  if (scaled > static_cast<double>(UINT_MAX)) { scaled = static_cast<double>(UINT_MAX); }

  auto numerator = static_cast<unsigned int>(std::round(scaled));
  if (numerator == 0U) { numerator = 1U; }

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
