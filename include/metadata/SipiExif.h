/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * This file implements the virtual abstract class which implements the image file I/O.
 */
#ifndef defined_exif_h
#define defined_exif_h


#include <string>
#include <vector>

#include <exiv2/error.hpp>
#include <exiv2/exif.hpp>
#include <exiv2/tags.hpp>
#include <exiv2/types.hpp>
#include <exiv2/value.hpp>

#include "SipiError.hpp"

namespace Sipi {

/**
 * @class SipiXmp
 * @author Lukas Rosenthaler
 * @version 0.1
 *
 * This class handles Exif metadata. It uses the Exiv2 library
 *
 * There is a problem that the libtiff libraray accesses the EXIF data tag by tag and is not able
 * to pass or get the the EXIF data as a blob. All other libraries pass EXIF data as a blob that
 * can be handled by exiv2. Therefore there are methods to get/add EXIF-data tagwise.
 * A list of all valid EXIF tags can be found at http://www.exiv2.org/tags.html .
 */
class SipiExif
{
private:
  unsigned char *binaryExif;
  unsigned int binary_size;
  Exiv2::ExifData exifData;//!< Private member variable holding the exiv2 EXIF data
  Exiv2::ByteOrder byteorder;//!< Private member holding the byteorder of the EXIF data

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, std::string &val)
  {
    val = v->toString();
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<std::string> &val)
  {
    for (int i = 0; i < v->count(); i++) { val.push_back(v->toString(i)); }
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, char &val)
  {
    val = static_cast<char>(v->toInt64());
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<char> &val)
  {
    for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<char>(v->toInt64(i))); }
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, unsigned char &val)
  {
    val = static_cast<unsigned char>(v->toInt64());
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<unsigned char> &val)
  {
    for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<unsigned char>(v->toInt64(i))); }
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, short &val)
  {
    val = static_cast<short>(v->toInt64());
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<short> &val)
  {
    for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<short>(v->toInt64(i))); }
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, unsigned short &val)
  {
    val = static_cast<unsigned short>(v->toInt64());
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<unsigned short> &val)
  {
    for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<unsigned short>(v->toInt64(i))); }
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, int &val)
  {
    val = static_cast<int>(v->toInt64());
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<int> &val)
  {
    for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<int>(v->toInt64(i))); }
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, unsigned int &val)
  {
    val = static_cast<unsigned int>(v->toInt64());
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<unsigned int> &val)
  {
    for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<unsigned int>(v->toInt64(i))); }
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, float &val)
  {
    val = static_cast<float>(v->toFloat());
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<float> &val)
  {
    for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<float>(v->toFloat(i))); }
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, double &val)
  {
    val = static_cast<double>(v->toFloat());
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<double> &val)
  {
    for (int i = 0; i < v->count(); i++) { val.push_back(static_cast<double>(v->toFloat(i))); }
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, Exiv2::Rational &val)
  {
    val = v->toRational();
    return v->ok();
  }

  static inline bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<Exiv2::Rational> &val)
  {
    for (int i = 0; i < v->count(); i++) { val.push_back(v->toRational(i)); }
    return v->ok();
  }

public:
  /*!
   * Constructor (default)
   */
  SipiExif();


  /*!
   * Constructor using an EXIF blob
   *
   * \param[in] exif Buffer containing the EXIF data
   * \Param[in] len Length of the EXIF buffer
   */
  SipiExif(const unsigned char *exif, unsigned int len);


  ~SipiExif();

  /*!
   * Returns the bytes of the EXIF data
   *
   * \param[out] len Length of buffer returned
   * \returns Buffer with EXIF data
   */
  unsigned char *exifBytes(unsigned int &len);

  /*!
   * Returns the bytes of the EXIF data as vector
   *
   * @return Vector with EXIF data
   */
  std::vector<unsigned char> exifBytes();

  /*!
   * Helper function to convert a signed float to a signed rational as used by EXIF
   *
   * \param[in] f Input signed float
   * \returns Exiv2::Rational
   */
  static Exiv2::Rational toRational(float f);

  /*!
   * Helper function to convert a unsigned float to a unsigned rational as used by EXIF
   *
   * \param[in] f Input unsigned float
   * \returns Exiv2::URational
   */
  static Exiv2::URational toURational(float f);

  template<class T> void addKeyVal(const std::string &key_p, const T &val) { exifData[key_p] = val; }

  template<class T> void addKeyVal(uint16_t tag, const std::string &groupName, const T &val)
  {
    Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
    Exiv2::Value::UniquePtr v;
    if (typeid(T) == typeid(std::string)) {
      v = Exiv2::Value::create(Exiv2::asciiString);
    } else if (typeid(T) == typeid(int8_t)) {
      v = Exiv2::Value::create(Exiv2::signedByte);
    } else if (typeid(T) == typeid(uint8_t)) {
      v = Exiv2::Value::create(Exiv2::unsignedByte);
    } else if (typeid(T) == typeid(int16_t)) {
      v = Exiv2::Value::create(Exiv2::signedShort);
    } else if (typeid(T) == typeid(uint16_t)) {
      v = Exiv2::Value::create(Exiv2::unsignedShort);
    } else if (typeid(T) == typeid(int32_t)) {
      v = Exiv2::Value::create(Exiv2::signedLong);
    } else if (typeid(T) == typeid(uint32_t)) {
      v = Exiv2::Value::create(Exiv2::unsignedLong);
    } else if (typeid(T) == typeid(float)) {
      v = Exiv2::Value::create(Exiv2::tiffFloat);
    } else if (typeid(T) == typeid(double)) {
      v = Exiv2::Value::create(Exiv2::tiffDouble);
    } else if (typeid(T) == typeid(Exiv2::Rational)) {
      v = Exiv2::Value::create(Exiv2::signedRational);
    } else if (typeid(T) == typeid(Exiv2::URational)) {
      v = Exiv2::Value::create(Exiv2::unsignedRational);
    } else {
      throw SipiError("Unsupported type of addKeyVal(2)");
    }

    v->read((unsigned char *)&val, sizeof(T), Exiv2::littleEndian);
    exifData.add(key, v.get());
  }

  template<class T> void addKeyVal(uint16_t tag, const std::string &groupName, const T *valptr, size_t len)
  {
    Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
    Exiv2::Value::UniquePtr v;
    if (typeid(T) == typeid(int8_t)) {
      v = Exiv2::Value::create(Exiv2::signedByte);
    } else if (typeid(T) == typeid(uint8_t)) {
      v = Exiv2::Value::create(Exiv2::unsignedByte);
    } else if (typeid(T) == typeid(int16_t)) {
      v = Exiv2::Value::create(Exiv2::signedShort);
    } else if (typeid(T) == typeid(uint16_t)) {
      v = Exiv2::Value::create(Exiv2::unsignedShort);
    } else if (typeid(T) == typeid(int32_t)) {
      v = Exiv2::Value::create(Exiv2::signedLong);
    } else if (typeid(T) == typeid(int32_t)) {
      v = Exiv2::Value::create(Exiv2::unsignedLong);
    } else if (typeid(T) == typeid(float)) {
      v = Exiv2::Value::create(Exiv2::tiffFloat);
    } else if (typeid(T) == typeid(double)) {
      v = Exiv2::Value::create(Exiv2::tiffDouble);
    } else if (typeid(T) == typeid(Exiv2::Rational)) {
      v = Exiv2::Value::create(Exiv2::signedRational);
    } else if (typeid(T) == typeid(Exiv2::URational)) {
      v = Exiv2::Value::create(Exiv2::unsignedRational);
    } else {
      throw SipiError("Unsupported type of addKeyVal(2)");
    }
    v->read((unsigned char *)valptr, static_cast<long>(len * sizeof(unsigned short)), Exiv2::littleEndian);
    exifData.add(key, v.get());
  }

  //............................................................................
  // Getting values from the EXIF object
  //

  //____________________________________________________________________________
  // string values
  //
  template<class T> bool getValByKey(const std::string &key_p, T &val)
  {
    try {
      Exiv2::ExifKey key = Exiv2::ExifKey(key_p);
      auto pos = exifData.findKey(key);
      if (pos == exifData.end()) { return false; }
      auto v = pos->getValue();
      return assign_val(v, val);
    } catch (const Exiv2::Error &err) {
      return false;
    }
  }

  template<class T> bool getValByKey(uint16_t tag, const std::string &groupName, T &val)
  {
    try {
      Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
      auto pos = exifData.findKey(key);
      if (pos == exifData.end()) { return false; }
      auto v = pos->getValue();
      return assign_val(v, val);

    } catch (const Exiv2::Error &err) {
      return false;
    }
  }

  friend std::ostream &operator<<(std::ostream &lhs, SipiExif &rhs);
};

}// namespace Sipi

#endif
