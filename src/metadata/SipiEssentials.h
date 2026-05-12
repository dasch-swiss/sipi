/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef __defined_essentials_h
#define __defined_essentials_h

#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>


#include "shttps/Hash.h"

namespace Sipi {

/*!
 * Small class to create a small essential metadata packet that can be embedded
 * within an image header. It contains
 * - original file name
 * - original mime type
 * - checksum method
 * - checksum of uncompressed pixel values
 * The class contains methods to set the fields, to serialize and deserialize
 * the data.
 */
class SipiEssentials
{
private:
  bool _is_set;
  std::string _origname;//!< original filename
  std::string _mimetype;//!< original mime type
  shttps::HashType _hash_type;//!< type of checksum
  std::string _data_chksum;//!< the checksum of pixel data
  bool _use_icc;//!< use this ICC profile when converting from JPEG200 to other format
  std::string _icc_profile;//!< ICC profile if JPEG2000 can not deal with it directly

public:
  /*!
   * Constructor for empty packet
   */
  inline SipiEssentials()
  {
    _hash_type = shttps::HashType::none;
    _is_set = false;
    _use_icc = false;
  }

  /*!
   * Constructor where all fields are passed
   *
   * \param[in] origname_p Original filename
   * \param[in] mimetype_p Original mimetype as string
   * \param[in] hash_type_p Checksumtype as defined in Hash.h (shttps::HashType)
   * \param[in] data_chksum_p The actual checksum of the internal image data
   * \param[in] icc_profile_p ICC profile data
   */
  SipiEssentials(const std::string &origname_p,
    const std::string &mimetype_p,
    shttps::HashType hash_type_p,
    const std::string &data_chksum_p,
    const std::vector<unsigned char> &icc_profile_p = std::vector<unsigned char>());

  /*!
   * Constructor taking a serialized packet (as string)
   *
   * \param[in] datastr Serialzed metadata packet
   */
  inline SipiEssentials(const std::string &datastr) { parse(datastr); }

  /*!
   * Getter for original name
   */
  inline std::string origname(void) { return _origname; }

  /*!
   * Setter for original name
   */
  inline void origname(const std::string &origname_p)
  {
    _origname = origname_p;
    _is_set = true;
  }

  /*!
   * Getter for mimetype
   */
  inline std::string mimetype(void) { return _mimetype; }

  /*!
   * Setter for original name
   */
  inline void mimetype(const std::string &mimetype_p) { _mimetype = mimetype_p; }

  /*!
   * Getter for checksum type as shttps::HashType
   */
  inline shttps::HashType hash_type(void) { return _hash_type; }

  /*!
   * Getter for checksum type as string
   */
  std::string hash_type_string(void) const;

  /*!
   * Setter for checksum type
   *
   * \param[in] hash_type_p checksum type
   */
  inline void hash_type(shttps::HashType hash_type_p) { _hash_type = hash_type_p; }

  /*!
   * Setter for checksum type
   *
   * \param[in] hash_type_p checksum type
   */
  void hash_type(const std::string &hash_type_p);

  /*!
   * Getter for checksum
   *
   * @return Chekcsum as std::string
   */
  inline std::string data_chksum(void) { return _data_chksum; }

  /*!
   * Setter for checksum
   */
  inline void data_chksum(const std::string &data_chksum_p) { _data_chksum = data_chksum_p; }

  /*!
   * Do I have to use this ICC profile? (only when converting from JPEG2000
   * @return Bool if this ICC profile should be used....
   */
  inline bool use_icc(void) { return _use_icc; }

  /*!
   * Setter for boolean flag for the usage of the essential's ICC profile
   * @param use_icc_p
   */
  inline void use_icc(bool use_icc_p) { _use_icc = use_icc_p; }


  /*!
   * Getter for ICC profile
   *
   * @return ICC profile binary data as std::vector
   */
  std::vector<unsigned char> icc_profile(void);

  unsigned char *icc_profile(unsigned int &len);
  /*!
   * Setter for ICC profile
   *
   * \param[in] icc_profile_p ICC profile binary data as std::vector
   */
  void icc_profile(const std::vector<unsigned char> &icc_profile_p);

  /*!
   * Parse a string containing a serialized metadata packet
   */
  void parse(const std::string &str);

  /*!
   * Check if essential metadata has been set.
   */
  inline bool is_set(void) { return _is_set; }

  /*!
   * String conversion operator
   */
  inline operator std::string() const
  {
    std::string tmpstr = _origname + "|" + _mimetype + "|" + hash_type_string() + "|" + _data_chksum;
    if (_use_icc)
      tmpstr += "|USE_ICC|";
    else
      tmpstr += "|IGNORE_ICC|";
    if (!_icc_profile.empty())
      tmpstr += _icc_profile;
    else
      tmpstr += "NULL";
    return tmpstr;
  }

  /*!
   * Stream output operator
   */
  inline friend std::ostream &operator<<(std::ostream &ostr, const SipiEssentials &rhs)
  {
    ostr << rhs._origname << "|" << rhs._mimetype << "|" << rhs.hash_type_string() << "|" << rhs._data_chksum;
    if (rhs._use_icc)
      ostr << "|USE_ICC|";
    else
      ostr << "|IGNORE_ICC|";
    if (!rhs._icc_profile.empty())
      ostr << rhs._icc_profile;
    else
      ostr << "NULL";
    return ostr;
  }
};
}// namespace Sipi


#endif
