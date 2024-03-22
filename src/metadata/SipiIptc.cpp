/*
 * Copyright © 2016 Lukas Rosenthaler, Andrea Bianco, Benjamin Geer,
 * Ivan Subotic, Tobias Schweizer, André Kilchenmann, and André Fatton.
 * This file is part of Sipi.
 * Sipi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * Sipi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Additional permission under GNU AGPL version 3 section 7:
 * If you modify this Program, or any covered work, by linking or combining
 * it with Kakadu (or a modified version of that library) or Adobe ICC Color
 * Profiles (or a modified version of that library) or both, containing parts
 * covered by the terms of the Kakadu Software Licence or Adobe Software Licence,
 * or both, the licensors of this Program grant you additional permission
 * to convey the resulting work.
 * See the GNU Affero General Public License for more details.
 * You should have received a copy of the GNU Affero General Public
 * License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "../SipiError.hpp"
#include "SipiIptc.h"
#include <stdlib.h>

static const char __file__[] = __FILE__;

namespace Sipi {

    SipiIptc::SipiIptc(const unsigned char *iptc, unsigned int len) {
        if (Exiv2::IptcParser::decode(iptcData, iptc, (uint32_t) len) != 0) {
            throw SipiError(__file__, __LINE__, "No valid IPTC data!");
        }
    }
    //============================================================================

    SipiIptc::~SipiIptc() {}


    unsigned char * SipiIptc::iptcBytes(unsigned int &len) {
        Exiv2::DataBuf databuf = Exiv2::IptcParser::encode(iptcData);
        unsigned char *buf = new unsigned char[databuf.size_];
        memcpy (buf, databuf.pData_, databuf.size_);
        len = databuf.size_;
        return buf;
    }
    //============================================================================

    std::vector<unsigned char> SipiIptc::iptcBytes(void) {
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

    std::ostream &operator<< (std::ostream &outstr, SipiIptc &rhs) {
        Exiv2::IptcData::iterator end = rhs.iptcData.end();
        for (Exiv2::IptcData::iterator md = rhs.iptcData.begin(); md != end; ++md) {
            outstr << std::setw(44) << std::setfill(' ') << std::left
                << md->key() << " "
                << "0x" << std::setw(4) << std::setfill('0') << std::right
                << std::hex << md->tag() << " "
                << std::setw(9) << std::setfill(' ') << std::left
                << md->typeName() << " "
                << std::dec << std::setw(3)
                << std::setfill(' ') << std::right
                << md->count() << "  "
                << std::dec << md->value()
                << std::endl;
        }
        return outstr;
    }
    //============================================================================

}
