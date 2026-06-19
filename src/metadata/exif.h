/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * This file implements the virtual abstract class which implements the image file I/O.
 */
#ifndef SIPI_METADATA_EXIF_H
#define SIPI_METADATA_EXIF_H


#include <string>
#include <vector>

#include <exiv2/error.hpp>
#include <exiv2/exif.hpp>
#include <exiv2/tags.hpp>
#include <exiv2/types.hpp>
#include <exiv2/value.hpp>

#include "SipiError.h"

namespace Sipi {

/**
 * @class Exif
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
class Exif
{
private:
  unsigned char *binaryExif;
  unsigned int binary_size;
  Exiv2::ExifData exifData;//!< Private member variable holding the exiv2 EXIF data
  Exiv2::ByteOrder byteorder;//!< Private member holding the byteorder of the EXIF data

  // Type-dispatched assignment helpers backing the inline `getValByKey<T>`
  // templates below. Definitions live in Exif.cpp so the 22 inline bodies
  // don't have to be parsed by every TU that includes this header (DEV-6407).
  static bool assign_val(Exiv2::Value::UniquePtr &v, std::string &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<std::string> &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, char &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<char> &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, unsigned char &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<unsigned char> &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, short &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<short> &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, unsigned short &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<unsigned short> &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, int &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<int> &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, unsigned int &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<unsigned int> &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, float &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<float> &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, double &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<double> &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, Exiv2::Rational &val);
  static bool assign_val(Exiv2::Value::UniquePtr &v, std::vector<Exiv2::Rational> &val);

public:
  /*!
   * Constructor (default)
   */
  Exif();


  /*!
   * Constructor using an EXIF blob
   *
   * \param[in] exif Buffer containing the EXIF data
   * \Param[in] len Length of the EXIF buffer
   */
  Exif(const unsigned char *exif, unsigned int len);


  ~Exif();

  /*!
   * Returns the bytes of the EXIF data as a vector.
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
    v->read((unsigned char *)valptr, static_cast<long>(len * sizeof(T)), Exiv2::littleEndian);
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

  friend std::ostream &operator<<(std::ostream &lhs, Exif &rhs);
};

}// namespace Sipi

#endif
