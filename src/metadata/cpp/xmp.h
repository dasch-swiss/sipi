/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * This file implements the virtual abstract class which implements the image file I/O.
 */
#ifndef SIPI_METADATA_XMP_H
#define SIPI_METADATA_XMP_H

#include <exiv2/error.hpp>
#include <exiv2/xmp_exiv2.hpp>//!< Import xmp from the exiv2 library!
#include <mutex>
#include <string>

namespace Sipi {

typedef struct
{
  std::mutex lock;
} XmpMutex;

extern XmpMutex xmp_mutex;

extern void xmplock_func(void *pLockData, bool lockUnlock);

/*!
 * This class handles XMP metadata. It uses the Exiv2 library
 */
class Xmp
{
private:
  Exiv2::XmpData xmpData;//!< Private member variable holding the exiv2 XMP data
  std::string __xmpstr;

public:
  /*!
   * Constructor
   *
   * \param[in] xmp A std::string containing RDF/XML with XMP data
   */
  Xmp(const std::string &xmp);

  /*!
   * Constructor
   *
   * \param[in] xmp A C-string (char *)containing RDF/XML with XMP data
   */
  Xmp(const char *xmp);

  /*!
   * Constructor
   *
   * \param[in] xmp A string containing RDF/XML with XMP data
   * \param[in] len Length of the string
   */
  Xmp(const char *xmp, int len);


  /*!
   * Destructor
   */
  ~Xmp();


  /*!
   * Returns the bytes of the RDF/XML data as a std::string.
   *
   * @return String holding the xmp data
   */
  std::string xmpBytes();

  /*!
   * The overloaded << operator which is used to write the xmp formatted to the outstream
   *
   * \param[in] lhs The output stream
   * \param[in] rhs Reference to an instance of a Xmp
   * \returns Returns ostream object
   */
  friend std::ostream &operator<<(std::ostream &lhs, const Xmp &rhs);
};

}// namespace Sipi

#endif
