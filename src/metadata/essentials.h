/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_METADATA_SIPI_ESSENTIALS_H
#define SIPI_METADATA_SIPI_ESSENTIALS_H

#include <ostream>
#include <string>
#include <vector>

#include "shttps/Hash.h"

namespace Sipi {

/*!
 * Plain value type holding the six fields embedded in SIPI's image-header
 * "essentials" packet. Per DEV-6408 (Probe 2): the public API is a struct
 * of accessors, not 17 getter/setter pairs.
 */
struct EssentialsFields
{
  std::string origname;//!< original filename
  std::string mimetype;//!< original mime type
  shttps::HashType hash_type = shttps::HashType::none;//!< type of checksum
  std::string data_chksum;//!< the checksum of pixel data
  bool use_icc = false;//!< apply this ICC profile when converting from JPEG2000
  std::vector<unsigned char> icc_profile;//!< decoded ICC profile bytes (empty if none)
};

/*!
 * Small class that creates a packet of essentials metadata embedded in an
 * image header (containing original filename, original mime type, checksum
 * method, pixel-data checksum, and optionally an ICC profile).
 *
 * The pipe-delimited wire format is retained indefinitely for reading legacy
 * packets per DEV-6398 scope; DEV-6410 (ADR-0005) adds CBOR for new emissions.
 */
class Essentials
{
private:
  bool _is_set = false;
  EssentialsFields _fields{};

public:
  Essentials() = default;

  /*!
   * Construct from fully-populated fields. Marks the packet as set.
   */
  explicit Essentials(EssentialsFields fields) : _is_set(true), _fields(std::move(fields)) {}

  /*!
   * Parse a serialized (pipe-delimited) packet. Marks the packet as set on
   * successful parse.
   */
  explicit Essentials(const std::string &serialized);

  [[nodiscard]] bool is_set() const { return _is_set; }

  [[nodiscard]] const EssentialsFields &fields() const { return _fields; }

  /*!
   * Mutable field access. Reaching for this implicitly marks the packet as set.
   */
  EssentialsFields &fields_mut()
  {
    _is_set = true;
    return _fields;
  }

  /*!
   * Serialize the packet to the pipe-delimited wire form used by SIPI's
   * format handlers (TIFF SIPIMETA tag, JPEG COM marker, JP2 codestream
   * comment, PNG text chunk).
   */
  [[nodiscard]] std::string serialize() const;

  friend std::ostream &operator<<(std::ostream &os, const Essentials &rhs) { return os << rhs.serialize(); }
};

}// namespace Sipi

#endif
