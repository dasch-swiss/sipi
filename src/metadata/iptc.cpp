/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "metadata/iptc.h"
#include "SipiError.h"

namespace Sipi {

Iptc::Iptc(const unsigned char *iptc, unsigned int len)
{
  if (Exiv2::IptcParser::decode(iptcData, iptc, (uint32_t)len) != 0) {
    throw SipiError("No valid IPTC data!");
  }
}
//============================================================================

Iptc::~Iptc() {}


std::vector<unsigned char> Iptc::iptcBytes()
{
  Exiv2::DataBuf databuf = Exiv2::IptcParser::encode(iptcData);
  if (databuf.size() == 0) return {};
  return std::vector<unsigned char>(databuf.data(), databuf.data() + databuf.size());
}
//============================================================================

std::ostream &operator<<(std::ostream &outstr, Iptc &rhs)
{
  Exiv2::IptcData::iterator end = rhs.iptcData.end();
  for (Exiv2::IptcData::iterator md = rhs.iptcData.begin(); md != end; ++md) {
    outstr << std::setw(44) << std::setfill(' ') << std::left << md->key() << " "
           << "0x" << std::setw(4) << std::setfill('0') << std::right << std::hex << md->tag() << " " << std::setw(9)
           << std::setfill(' ') << std::left << md->typeName() << " " << std::dec << std::setw(3) << std::setfill(' ')
           << std::right << md->count() << "  " << std::dec << md->value() << std::endl;
  }
  return outstr;
}
//============================================================================

}// namespace Sipi
