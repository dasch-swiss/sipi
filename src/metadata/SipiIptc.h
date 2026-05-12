/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * This file implements the virtual abstract class which implements the image file I/O.
 */
#ifndef __defined_iptc_h
#define __defined_iptc_h

#include <exiv2/iptc.hpp>
#include <string>
#include <vector>

namespace Sipi {

/*!
 * Handles IPTC data based on the exiv2 library
 */
class SipiIptc
{
private:
  Exiv2::IptcData iptcData;//!< Private member variable holding the exiv2 IPTC object

public:
  /*!
   * Constructor
   *
   * \param[in] Buffer containing the IPTC data in native format
   * \param[in] Length of the buffer
   */
  SipiIptc(const unsigned char *iptc, unsigned int len);

  /*!
   * Destructor
   */
  ~SipiIptc();

  /*!
   * Returns the bytes of the IPTC data. The buffer must be
   * deleted by the caller after it is no longer used!
   * \param[out] len Length of the data in bytes
   * \returns Chunk of chars holding the IPTC data
   */
  unsigned char *iptcBytes(unsigned int &len);

  /*!
   * Returns the bytes of the IPTC data as std::vector
   * @return IPTC bytes as std::vector
   */
  std::vector<unsigned char> iptcBytes(void);

  /*!
   * The overloaded << operator which is used to write the IPTC data formatted to the outstream
   *
   * \param[in] lhs The output stream
   * \param[in] rhs Reference to an instance of a SipiIptc
   * \returns Returns ostream object
   */
  friend std::ostream &operator<<(std::ostream &lhs, SipiIptc &rhs);
};

}// namespace Sipi

#endif
