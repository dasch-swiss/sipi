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
#include <climits>
#include <cmath>

#include "../SipiError.hpp"
#include "SipiExif.h"

static const char file_[] = __FILE__;

namespace Sipi {

    SipiExif::SipiExif() {
        binaryExif = nullptr;
        binary_size = 0;
        byteorder = Exiv2::littleEndian; // that's today's default....
    };
    //============================================================================


    SipiExif::SipiExif(const unsigned char *exif, unsigned int len) {
        //
        // first we save the binary exif... we use it later for constructing a binary exif again!
        //
        binaryExif = new unsigned char[len];
        memcpy (binaryExif, exif, len);
        binary_size = len;

        //
        // now we decode the binary exif
        //
        try {
            byteorder = Exiv2::ExifParser::decode(exifData, exif, (uint32_t) len);
        }
        catch(Exiv2::BasicError<char> &exiverr) {
            throw SipiError(file_, __LINE__, exiverr.what());
        }
    }
    //============================================================================

    SipiExif::~SipiExif() {
        delete [] binaryExif;
    }

    unsigned char * SipiExif::exifBytes(unsigned int &len) {
        Exiv2::Blob blob;
        Exiv2::WriteMethod wm = Exiv2::ExifParser::encode(blob, binaryExif, binary_size, byteorder, exifData);
        unsigned char *tmpbuf;
        if (wm == Exiv2::wmIntrusive) {
            // we use blob
            binary_size = blob.size();
            tmpbuf = new unsigned char[binary_size];
            memcpy (tmpbuf, blob.data(), binary_size);
            delete [] binaryExif; // cleanup tmpbuf!
            binaryExif = tmpbuf;
        }
        len = binary_size;
        return binaryExif;
    }
    //============================================================================

    std::vector<unsigned char> SipiExif::exifBytes() {
        unsigned int len = 0;
        unsigned char *buf = exifBytes(len);
        std::vector<unsigned char> data(buf, buf + len);
        return data;
    }
    //============================================================================

    Exiv2::Rational SipiExif::toRational(float f) {
        int numerator;
        int denominator;
        if (f == 0.0F) {
            numerator = 0;
            denominator = 1;
        }
        else if (f == floorf(f)) {
            numerator = (int) f;
            denominator = 1;
        }
        else if (f > 0.0F) {
            if (f < 1.0F) {
                numerator = (int) (f*static_cast<float>(LONG_MAX));
                denominator = INT_MAX;
            }
            else {
                numerator = INT_MAX;
                denominator = (int) (static_cast<float>(INT_MAX) / f);
            }
        }
        else {
            if (f > -1.0F) {
                numerator = (int) (f*static_cast<float>(INT_MAX));
                denominator = INT_MAX;
            }
            else {
                numerator = INT_MAX;
                denominator = (int) (static_cast<float>(INT_MAX) / f);
            }
        }
        return std::make_pair(numerator, denominator);
    }
    //============================================================================

    Exiv2::URational SipiExif::toURational(float f) {
        unsigned int numerator;
        unsigned int denominator;

        if (f < 0.0F) throw SipiError(file_, __LINE__, "Cannot convert negative float to URational!");

        if (f == 0.0F) {
            numerator = 0;
            denominator = 1;
        }
        else if (f == (float)((int) f)) {
            numerator = (int) f;
            denominator = 1;
        }
        if (f < 1.0F) {
            numerator = (int) (f*static_cast<float>(UINT_MAX));
            denominator = UINT_MAX;
        }
        else {
            numerator = UINT_MAX;
            denominator = (int) (static_cast<float>(UINT_MAX) / f);
        }
        return std::make_pair(numerator, denominator);
    }
    //============================================================================


    std::ostream &operator<< (std::ostream &outstr, SipiExif &rhs) {
        Exiv2::ExifData::const_iterator end = rhs.exifData.end();
        for (Exiv2::ExifData::const_iterator i = rhs.exifData.begin(); i != end; ++i) {
            const char* tn = i->typeName();
            outstr << std::setw(44) << std::setfill(' ') << std::left
                << i->key() << " "
                << "0x" << std::setw(4) << std::setfill('0') << std::right
                << std::hex << i->tag() << " "
                << std::setw(9) << std::setfill(' ') << std::left
                << (tn ? tn : "Unknown") << " "
                << std::dec << std::setw(3)
                << std::setfill(' ') << std::right
                << i->count() << "  "
                << std::dec << i->value()
                << "\n";
        }
        return outstr;
    }
    //============================================================================

}
