/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "SipiIptc.h"
#include "../SipiError.hpp"
#include <stdlib.h>

static const char __file__[] = __FILE__;

namespace Sipi {

SipiIptc::SipiIptc(const unsigned char *iptc, unsigned int len)
{
  if (Exiv2::IptcParser::decode(iptcData, iptc, (uint32_t)len) != 0) {
    throw SipiError(__file__, __LINE__, "No valid IPTC data!");
  }
}
//============================================================================

SipiIptc::~SipiIptc() {}


unsigned char *SipiIptc::iptcBytes(unsigned int &len)
{
  Exiv2::DataBuf databuf = Exiv2::IptcParser::encode(iptcData);
  unsigned char *buf = new unsigned char[databuf.size_];
  memcpy(buf, databuf.pData_, databuf.size_);
  len = databuf.size_;
  return buf;
}
//============================================================================

std::vector<unsigned char> SipiIptc::iptcBytes(void)
{
  unsigned int len = 0;
  unsigned char *buf = iptcBytes(len);
  std::vector<unsigned char> data;
  if (buf != nullptr) {
    data.reserve(len);
    for (int i = 0; i < len; i++) data.push_back(buf[i]);
    delete[] buf;
  }
  return data;
}
//============================================================================

std::ostream &operator<<(std::ostream &outstr, SipiIptc &rhs)
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
