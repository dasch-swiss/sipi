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

#include "SipiError.h"
#include "SipiExif.h"

static const char __file__[] = __FILE__;

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
            throw SipiError(__file__, __LINE__, exiverr.what());
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

    std::vector<unsigned char> SipiExif::exifBytes(void) {
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
                numerator = (int) (f*LONG_MAX);
                denominator = INT_MAX;
            }
            else {
                numerator = INT_MAX;
                denominator = (int) (INT_MAX / f);
            }
        }
        else {
            if (f > -1.0F) {
                numerator = (int) (f*INT_MAX);
                denominator = INT_MAX;
            }
            else {
                numerator = INT_MAX;
                denominator = (int) (INT_MAX / f);
            }
        }
        return std::make_pair(numerator, denominator);
    }
    //============================================================================

    Exiv2::URational SipiExif::toURational(float f) {
        unsigned int numerator;
        unsigned int denominator;

        if (f < 0.0F) throw SipiError(__file__, __LINE__, "Cannot convert negative float to URational!");

        if (f == 0.0F) {
            numerator = 0;
            denominator = 1;
        }
        else if (f == (float)((int) f)) {
            numerator = (int) f;
            denominator = 1;
        }
        if (f < 1.0F) {
            numerator = (int) (f*UINT_MAX);
            denominator = UINT_MAX;
        }
        else {
            numerator = UINT_MAX;
            denominator = (int) (UINT_MAX / f);
        }
        return std::make_pair(numerator, denominator);
    }
    //============================================================================

    void SipiExif::addKeyVal(const std::string &key_p, const std::string &val_p) {
        exifData[key_p.c_str()] = val_p.c_str();
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const std::string &val_p) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::asciiString);
        v->read((unsigned char *) val_p.c_str(), val_p.size(), Exiv2::littleEndian);
        exifData.add(key, v.get());
    }
    //============================================================================

    //............................................................................
    // float values and float arrays
    //
    void SipiExif::addKeyVal(const std::string &key_p, const float &val_p) {
        exifData[key_p] = toRational(val_p);
    }
    //============================================================================

    void SipiExif::addKeyVal(const std::string &key_p, const float *vals_p, unsigned int len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::tiffFloat);
        v->read((unsigned char *) vals_p, len*sizeof(float), Exiv2::littleEndian);
        Exiv2::ExifKey key(key_p);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const float &val_p) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::tiffFloat);
        v->read((unsigned char *) &val_p, sizeof(float), Exiv2::littleEndian);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const float *vals_p, unsigned int len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::tiffFloat);
        v->read((unsigned char *) vals_p, len*sizeof(float), Exiv2::littleEndian);
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        exifData.add(key, v.get());
    }
    //============================================================================

    //............................................................................
    // short values and short arrays
    //
    void SipiExif::addKeyVal(const std::string &key_p, const short &val_p) {
        exifData[key_p] = val_p;
    }
    //============================================================================

    void SipiExif::addKeyVal(const std::string &key_p, const short *vals_p, unsigned int len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::signedShort);
        v->read((unsigned char *) vals_p, len*sizeof(short), Exiv2::littleEndian);
        Exiv2::ExifKey key(key_p);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const short &val_p) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::signedShort );
        v->read((unsigned char *) &val_p, sizeof(short), Exiv2::littleEndian);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const short *vals_p, unsigned int len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::signedShort);
        v->read((unsigned char *) vals_p, len*sizeof(short), Exiv2::littleEndian);
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        exifData.add(key, v.get());
    }
    //============================================================================

    //............................................................................
    // unsigned short values and unsigned short arrays
    //
    void SipiExif::addKeyVal(const std::string &key_p, const unsigned short &val_p) {
        exifData[key_p] = val_p;
    }
    //============================================================================

    void SipiExif::addKeyVal(const std::string &key_p, const unsigned short *vals_p, int unsigned len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::unsignedShort);
        v->read((unsigned char *) vals_p, len*sizeof(unsigned short), Exiv2::littleEndian);
        Exiv2::ExifKey key(key_p);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const unsigned short &val_p) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::unsignedShort);
        v->read((unsigned char *) &val_p, sizeof(unsigned short), Exiv2::littleEndian);
        exifData.add(key, v.get());
    }
    //============================================================================


    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const unsigned short *vals_p, unsigned int len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::unsignedShort);
        v->read((unsigned char *) vals_p, len*sizeof(unsigned short), Exiv2::littleEndian);
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        exifData.add(key, v.get());
    }
    //============================================================================


    //............................................................................
    // int values and int arrays
    //
    void SipiExif::addKeyVal(const std::string &key_p, const int &val_p) {
        exifData[key_p] = val_p;
    }
    //============================================================================

    void SipiExif::addKeyVal(const std::string &key_p, const int *vals_p, int unsigned len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::signedLong);
        v->read((unsigned char *) vals_p, len*sizeof(int), Exiv2::littleEndian);
        Exiv2::ExifKey key(key_p);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const int &val_p) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::signedLong);
        v->read((unsigned char *) &val_p, sizeof(int), Exiv2::littleEndian);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const int *vals_p, unsigned int len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::signedLong);
        v->read((unsigned char *) vals_p, len*sizeof(int), Exiv2::littleEndian);
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        exifData.add(key, v.get());
    }
    //============================================================================

    //............................................................................
    // unsigned int values and unsigned int arrays
    //
    void SipiExif::addKeyVal(const std::string &key_p, const unsigned int &val_p) {
        exifData[key_p] = val_p;
    }
    //============================================================================

    void SipiExif::addKeyVal(const std::string &key_p, const unsigned int *vals_p, unsigned int len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::unsignedLong);
        v->read((unsigned char *) vals_p, len*sizeof(unsigned int), Exiv2::littleEndian);
        Exiv2::ExifKey key(key_p);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const unsigned int &val_p) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::unsignedLong);
        v->read((unsigned char *) &val_p, sizeof(unsigned int), Exiv2::littleEndian);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const unsigned int *vals_p, unsigned int len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::unsignedLong);
        v->read((unsigned char *) vals_p, len*sizeof(unsigned int), Exiv2::littleEndian);
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        exifData.add(key, v.get());
    }
    //============================================================================

    //............................................................................
    // Rational values and Rational arrays
    //
    void SipiExif::addKeyVal(const std::string &key_p, const Exiv2::Rational &r) {
        exifData[key_p] = r;
    }
    //============================================================================

    void SipiExif::addKeyVal(const std::string &key_p, const Exiv2::Rational *vals_p, unsigned int len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::signedRational);
        v->read((unsigned char *) vals_p, len*sizeof(Exiv2::Rational), Exiv2::littleEndian);
        Exiv2::ExifKey key(key_p);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(const std::string &key_p, const int &val1_p, const int &val2_p) {
        exifData[key_p] = Exiv2::Rational(val1_p, val2_p);
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const Exiv2::Rational &r) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::signedRational);
        v->read((unsigned char *) &r, sizeof(Exiv2::Rational), Exiv2::littleEndian);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const Exiv2::Rational *vals_p, unsigned int len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::signedRational);
        v->read((unsigned char *) vals_p, len*sizeof(Exiv2::Rational), Exiv2::littleEndian);
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const int &val1_p, const int &val2_p) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::signedRational);
        Exiv2::Rational val = Exiv2::Rational(val1_p, val2_p);
        v->read((unsigned char *) &val, sizeof(Exiv2::Rational), Exiv2::littleEndian);
        exifData.add(key, v.get());
    }
    //============================================================================

    //............................................................................
    // unsigned Rational values and unsigned Rational arrays
    //
    void SipiExif::addKeyVal(const std::string &key_p, const Exiv2::URational &ur) {
        exifData[key_p] = ur;
    }
    //============================================================================

    void SipiExif::addKeyVal(const std::string &key_p, const Exiv2::URational *vals_p, unsigned int len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::unsignedRational);
        v->read((unsigned char *) vals_p, len*sizeof(Exiv2::URational), Exiv2::littleEndian);
        Exiv2::ExifKey key(key_p);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(const std::string &key_p, const unsigned int &val1_p, const unsigned int &val2_p) {
        exifData[key_p] = Exiv2::URational(val1_p, val2_p);
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const Exiv2::URational &ur) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::unsignedRational);
        v->read((unsigned char *) &ur, sizeof(Exiv2::URational), Exiv2::littleEndian);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const Exiv2::URational *vals_p, unsigned int len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::unsignedRational);
        v->read((unsigned char *) vals_p, len*sizeof(Exiv2::URational), Exiv2::littleEndian);
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const unsigned int &val1_p, const unsigned int &val2_p) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::unsignedRational);
        Exiv2::URational val = Exiv2::URational(val1_p, val2_p);
        v->read((unsigned char *) &val, sizeof(Exiv2::URational), Exiv2::littleEndian);
        exifData.add(key, v.get());
    }
    //============================================================================


    //............................................................................
    // Undefined data
    //
    void SipiExif::addKeyVal(const std::string &key_p, const unsigned char *vals_p, unsigned int len) {
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::undefined);
        v->read(vals_p, len*sizeof(unsigned char), Exiv2::littleEndian);
        Exiv2::ExifKey key(key_p);
        exifData.add(key, v.get());
    }
    //============================================================================

    void SipiExif::addKeyVal(uint16_t tag, const std::string &groupName, const unsigned char *vals_p, unsigned int len) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::Value::AutoPtr v = Exiv2::Value::create(Exiv2::undefined);
        v->read(vals_p, len*sizeof(unsigned char), Exiv2::littleEndian);
        exifData.add(key, v.get());
    }
    //============================================================================

    //............................................................................
    // Getting values from the EXIF object
    //

    //____________________________________________________________________________
    // string values
    //
    bool SipiExif::getValByKey(const std::string key_p, std::string &str_p) {
        try {
            Exiv2::ExifKey key = Exiv2::ExifKey(key_p);
            Exiv2::ExifData::iterator pos = exifData.findKey(key);
            if (pos == exifData.end()) {
                return false;
            }
            Exiv2::Value::AutoPtr v = pos->getValue();
            str_p = v->toString();
            return v->ok();
        } catch (const Exiv2::BasicError<char> &err) {
            return false;
        }
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, std::string &str_p) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        str_p = v->toString();
        return v->ok();
    }
    //============================================================================

    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, std::vector<std::string> &str_p) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        for (int i = 0; i < v->count(); i++) {
            str_p.push_back(v->toString(i));
        }
        return v->ok();
    }
    //============================================================================


    //____________________________________________________________________________
    // char values
    //
    bool SipiExif::getValByKey(const std::string key_p, char &c) {
        try {
            Exiv2::ExifKey key = Exiv2::ExifKey(key_p);
            Exiv2::ExifData::iterator pos = exifData.findKey(key);
            if (pos == exifData.end()) {
                return false;
            }
            Exiv2::Value::AutoPtr v = pos->getValue();
            c = (char) v->toLong();
            return v->ok();
        } catch (const Exiv2::BasicError<char> &err) {
            return false;
        }
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, char &c) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        c = (char) v->toLong();
        return v->ok();
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, std::vector<char> &vc) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        for (int i = 0; i < v->count(); i++) {
            vc.push_back((char) v->toLong(i));
        }
        return v->ok();
    }
    //============================================================================

    //____________________________________________________________________________
    // unsigned char values
    //
    bool SipiExif::getValByKey(const std::string key_p, unsigned char &uc) {
        try {
            Exiv2::ExifKey key = Exiv2::ExifKey(key_p);
            Exiv2::ExifData::iterator pos = exifData.findKey(key);
            if (pos == exifData.end()) {
                return false;
            }
            Exiv2::Value::AutoPtr v = pos->getValue();
            uc = (unsigned char) v->toLong();
            return v->ok();
        } catch (const Exiv2::BasicError<char> &err) {
            return false;
        }
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, unsigned char &uc) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        uc = (unsigned char) v->toLong();
        return v->ok();
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, std::vector<unsigned char> &vuc) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        for (int i = 0; i < v->count(); i++) {
            vuc.push_back((unsigned char) v->toLong(i));
        }
        return v->ok();
    }
    //============================================================================

    //____________________________________________________________________________
    // float values
    //
    bool SipiExif::getValByKey(const std::string key_p, float &f) {
        try {
            Exiv2::ExifKey key = Exiv2::ExifKey(key_p);
            Exiv2::ExifData::iterator pos = exifData.findKey(key);
            if (pos == exifData.end()) {
                return false;
            }
            Exiv2::Value::AutoPtr v = pos->getValue();
            f = v->toFloat();
            return v->ok();
        } catch (const Exiv2::BasicError<char> &err) {
            return false;
        }
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, float &f) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        f = v->toFloat();
        return v->ok();
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, std::vector<float> &f) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        for (int i = 0; i < v->count(); i++) {
            f.push_back(v->toFloat(i));
        }
        return v->ok();
    }
    //============================================================================


    //____________________________________________________________________________
    // Rational values
    //
    bool SipiExif::getValByKey(const std::string key_p, Exiv2::Rational &r) {
        std::cerr << "????????> SipiExif::getValByKey(const std::string key_p, Exiv2::Rational &r)" << std::endl;
        try {
            Exiv2::ExifKey key = Exiv2::ExifKey(key_p);
            Exiv2::ExifData::iterator pos = exifData.findKey(key);
            if (pos == exifData.end()) {
                return false;
            }
            Exiv2::Value::AutoPtr v = pos->getValue();
            auto t = v->typeId();
            std::cerr << "-----TYPEID=" << t << std::endl;
            r =  v->toRational();
            return v->ok();
        } catch (const Exiv2::BasicError<char> &err) {
            return false;
        }
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, Exiv2::Rational &r) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        r =  v->toRational();
        return v->ok();
    }
    //============================================================================


    bool SipiExif::getValByKey(const std::string key_p, std::vector<Exiv2::Rational> &r) {
        Exiv2::ExifKey key = Exiv2::ExifKey(key_p);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        for (int i = 0; i < v->count(); i++) {
            r.push_back(v->toRational(i));
        }
        return v->ok();
    }
    //============================================================================

    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, std::vector<Exiv2::Rational> &r) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        for (int i = 0; i < v->count(); i++) {
            r.push_back(v->toRational(i));
        }
        return v->ok();
    }
    //============================================================================


    //____________________________________________________________________________
    // short values
    //
    bool SipiExif::getValByKey(const std::string key_p, short &s) {
        try {
            Exiv2::ExifKey key = Exiv2::ExifKey(key_p);
            Exiv2::ExifData::iterator pos = exifData.findKey(key);
            if (pos == exifData.end()) {
                return false;
            }
            Exiv2::Value::AutoPtr v = pos->getValue();
            s = (short) v->toLong();
            return v->ok();
        } catch (const Exiv2::BasicError<char> &err) {
            return false;
        }
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, short &s) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        s = (short) v->toLong();
        return v->ok();
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, std::vector<short> &s) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        for (int i = 0; i < v->count(); i++) {
            s.push_back((short) v->toLong(i));
        }
        return v->ok();
    }
    //============================================================================


    //____________________________________________________________________________
    // unsigned short values
    //
    bool SipiExif::getValByKey(const std::string key_p, unsigned short &s) {
        try {
            Exiv2::ExifKey key = Exiv2::ExifKey(key_p);
            Exiv2::ExifData::iterator pos = exifData.findKey(key);
            if (pos == exifData.end()) {
                return false;
            }
            Exiv2::Value::AutoPtr v = pos->getValue();
            s = (unsigned short) v->toLong();
            return v->ok();
        } catch (const Exiv2::BasicError<char> &err) {
            return false;
        }
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, unsigned short &s) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        s = (unsigned short) v->toLong();
        return v->ok();
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, std::vector<unsigned short> &s) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        for (int i = 0; i < v->count(); i++) {
            s.push_back((unsigned short) v->toLong());
        }
        return v->ok();
    }
    //============================================================================


    //____________________________________________________________________________
    // int values
    //
    bool SipiExif::getValByKey(const std::string key_p, int &s) {
        try {
            Exiv2::ExifKey key = Exiv2::ExifKey(key_p);
            Exiv2::ExifData::iterator pos = exifData.findKey(key);
            if (pos == exifData.end()) {
                return false;
            }
            Exiv2::Value::AutoPtr v = pos->getValue();
            s = (int) v->toLong();
            return v->ok();
        } catch (const Exiv2::BasicError<char> &err) {
            return false;
        }
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, int &s) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        s = (int) v->toLong();
        return v->ok();
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, std::vector<int> &s) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        for (int i = 0; i < v->count(); i++) {
            s.push_back((int) v->toLong(i));
        }
        return v->ok();
    }
    //============================================================================


    //____________________________________________________________________________
    // unsigned int values
    //
    bool SipiExif::getValByKey(const std::string key_p, unsigned int &s) {
        try {
            Exiv2::ExifKey key = Exiv2::ExifKey(key_p);
            Exiv2::ExifData::iterator pos = exifData.findKey(key);
            if (pos == exifData.end()) {
                return false;
            }
            Exiv2::Value::AutoPtr v = pos->getValue();
            s = (unsigned int) v->toLong();
            return v->ok();
        } catch (const Exiv2::BasicError<char> &err) {
            return false;
        }
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, unsigned int &s) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        s = (unsigned int) v->toLong();
        return v->ok();
    }
    //============================================================================


    bool SipiExif::getValByKey(uint16_t tag, const std::string &groupName, std::vector<unsigned int> &s) {
        Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
        Exiv2::ExifData::iterator pos = exifData.findKey(key);
        if (pos == exifData.end()) {
            return false;
        }
        Exiv2::Value::AutoPtr v = pos->getValue();
        for (int i = 0; i < v->count(); i++) {
            s.push_back((unsigned int) v->toLong());
        }
        return v->ok();
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
