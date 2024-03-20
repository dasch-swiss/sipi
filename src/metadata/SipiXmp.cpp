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
#include <mutex>
#include <pthread.h>

#include "../SipiError.hpp"
#include "SipiXmp.h"

/*!
 * ToDo: remove provisional code as soon as Exiv2::Xmp is thread safe (expected v.26)
 * ATTENTION!!!!!!!!!
 * Since the Xmp-Part of Exiv2 Version 0.25 is not thread safe, we omit for the moment
 * the use of Exiv2::Xmp for processing XMP. We just transfer the XMP string as is. This
 * is bad, since we are not able to modifiy it. But we'll try again with Exiv2 v0.26!
 */

namespace Sipi {

    XmpMutex xmp_mutex;

    void xmplock_func(void *pLockData, bool lockUnlock) {
        auto *m = static_cast<XmpMutex *>(pLockData);
        if (lockUnlock) {
            std::cerr << "XMP-LOCK!" << std::endl;
            m->lock.lock();
        }
        else {
            std::cerr << "XMP-UNLOCK!" << std::endl;
            m->lock.unlock();
        }
    }
    //=========================================================================

    SipiXmp::SipiXmp(const std::string &xmp) {
        __xmpstr = xmp; // provisional code until Exiv2::Xmp is threadsafe
        return; // provisional code until Exiv2::Xmp is threadsafe
        // TODO: Testing required if now Exiv2::Xmp is thread save
        /*
        try {
            if (Exiv2::XmpParser::decode(xmpData, xmp) != 0) {
                Exiv2::XmpParser::terminate();
                throw SipiError(thisSourceFile, __LINE__, "Could not parse XMP!");
            }
        }
        catch(Exiv2::BasicError<char> &err) {
            throw SipiError(thisSourceFile, __LINE__, err.what());
        }
         */
    }
    //============================================================================

    SipiXmp::SipiXmp(const char *xmp) {
        __xmpstr = xmp; // provisional code until Exiv2::Xmp is threadsafe
        return; // provisional code until Exiv2::Xmp is threadsafe
        // TODO: Testing required if now Exiv2::Xmp is thread save
        /*
        try {
            if (Exiv2::XmpParser::decode(xmpData, xmp) != 0) {
                Exiv2::XmpParser::terminate();
                throw SipiError(thisSourceFile, __LINE__, "Could not parse XMP!");
            }
        }
        catch(Exiv2::BasicError<char> &err) {
            throw SipiError(thisSourceFile, __LINE__, err.what());
        }
         */
    }
    //============================================================================

    SipiXmp::SipiXmp(const char *xmp, int len) {
        std::string buf(xmp, len);
        __xmpstr = buf; // provisional code until Exiv2::Xmp is threadsafe
        return; // provisional code until Exiv2::Xmp is threadsafe
        // TODO: Testing required if now Exiv2::Xmp is thread save
        /*
        try {
            if (Exiv2::XmpParser::decode(xmpData, buf) != 0) {
                Exiv2::XmpParser::terminate();
                throw SipiError(thisSourceFile, __LINE__, "Could not parse XMP!");
            }
        }
        catch(Exiv2::BasicError<char> &err) {
            throw SipiError(thisSourceFile, __LINE__, err.what());
        }
         */
    }
    //============================================================================


    SipiXmp::~SipiXmp() {
        //Exiv2::XmpParser::terminate();
    }
    //============================================================================


    char * SipiXmp::xmpBytes(unsigned int &len) {
        char *__buf = new char[__xmpstr.length() + 1];
        memcpy (__buf, __xmpstr.c_str(), __xmpstr.length());
        __buf[__xmpstr.length()] = '\0';
        len = __xmpstr.length();
        return __buf; // provisional code until Exiv2::Xmp is threadsafe
        // TODO: Testing required if now Exiv2::Xmp is thread save

        /*
        std::string xmpPacket;
        try {
            if (0 != Exiv2::XmpParser::encode(xmpPacket, xmpData, Exiv2::XmpParser::useCompactFormat)) {
                throw SipiError(thisSourceFile, __LINE__, "Failed to serialize XMP data!");
            }
        }
        catch(Exiv2::BasicError<char> &err) {
            throw SipiError(thisSourceFile, __LINE__, err.what());
        }
        Exiv2::XmpParser::terminate();

        len = xmpPacket.size();
        char *buf = new char[len + 1];
        memcpy (buf, xmpPacket.c_str(), len);
        buf[len] = '\0';
        return buf;
         */
    }
    //============================================================================

    std::string SipiXmp::xmpBytes(void) {
        unsigned int len = 0;
        char *buf = xmpBytes(len);
        std::string data;
        if (buf != nullptr) {
            data.reserve(len);
            for (int i = 0; i < len; i++) data.push_back(buf[i]);
            delete[] buf;
        }
        return data;
    }
    //============================================================================

    std::ostream &operator<< (std::ostream &outstr, const SipiXmp &rhs) {
        /*
        for (Exiv2::XmpData::const_iterator md = rhs.xmpData.begin();
        md != rhs.xmpData.end(); ++md) {
            outstr << std::setfill(' ') << std::left
                << std::setw(44)
                << md->key() << " "
                << std::setw(9) << std::setfill(' ') << std::left
                << md->typeName() << " "
                << std::dec << std::setw(3)
                << std::setfill(' ') << std::right
                << md->count() << "  "
                << std::dec << md->value()
                << std::endl;
        }
         */
        return outstr;
    }
    //============================================================================

}
