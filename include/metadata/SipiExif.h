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
 *//*!
 * This file implements the virtual abstract class which implements the image file I/O.
 */
#ifndef defined_exif_h
#define defined_exif_h

#include <string>
#include <vector>
#include <exiv2/exiv2.hpp>

namespace Sipi {

    /**
     * @class SipiXmp
     * @author Lukas Rosenthaler
     * @version 0.1
     *
     * This class handles Exif metadata. It uses the Exiv2 library
     *
     * There is a problem that the libtiff libraray accesses the EXIF data tag by tag and is not able
     * to pass or get the the EXIF data as a blob. All other libraries pass EXIF data as a blob that
     * can be handled by exiv2. Therefore there are methods to get/add EXIF-data tagwise.
     * A list of all valid EXIF tags can be found at http://www.exiv2.org/tags.html .
     */
    class SipiExif {
    private:
        unsigned char *binaryExif;
        unsigned int binary_size;
        Exiv2::ExifData exifData;   //!< Private member variable holding the exiv2 EXIF data
        Exiv2::ByteOrder byteorder; //!< Private member holding the byteorder of the EXIF data

        inline bool assign_val(Exiv2::Value::AutoPtr &v, std::string &val) {
            val = v->toString();
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, std::vector<std::string> &val) {
            for (int i = 0; i < v->count(); i++) {
                val.push_back(v->toString(i));
            }
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, char &val) {
            val = static_cast<char>(v->toLong());
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, std::vector<char> &val) {
            for (int i = 0; i < v->count(); i++) {
                val.push_back(static_cast<char>(v->toLong(i)));
            }
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, unsigned char &val) {
            val = static_cast<unsigned char>(v->toLong());
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, std::vector<unsigned char> &val) {
            for (int i = 0; i < v->count(); i++) {
                val.push_back(static_cast<unsigned char>(v->toLong(i)));
            }
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, short val) {
            val = static_cast<short>(v->toLong());
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, std::vector<short> &val) {
            for (int i = 0; i < v->count(); i++) {
                val.push_back(static_cast<short>(v->toLong(i)));
            }
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, unsigned short &val) {
            val = static_cast<unsigned short>(v->toLong());
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, std::vector<unsigned short> &val) {
            for (int i = 0; i < v->count(); i++) {
                val.push_back(static_cast<unsigned short>(v->toLong(i)));
            }
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, int &val) {
            val = static_cast<int>(v->toLong());
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, std::vector<int> &val) {
            for (int i = 0; i < v->count(); i++) {
                val.push_back(static_cast<int>(v->toLong(i)));
            }
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, unsigned int &val) {
            val = static_cast<unsigned int>(v->toLong());
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, std::vector<unsigned int> &val) {
            for (int i = 0; i < v->count(); i++) {
                val.push_back(static_cast<unsigned int>(v->toLong(i)));
            }
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, float &val) {
            val = static_cast<float>(v->toFloat());
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, std::vector<float> &val) {
            for (int i = 0; i < v->count(); i++) {
                val.push_back(static_cast<float>(v->toFloat(i)));
            }
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, double &val) {
            val = static_cast<double>(v->toFloat());
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, std::vector<double> &val) {
            for (int i = 0; i < v->count(); i++) {
                val.push_back(static_cast<double>(v->toFloat(i)));
            }
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, Exiv2::Rational &val) {
            val = v->toRational();
            return v->ok();
        }

        inline bool assign_val(Exiv2::Value::AutoPtr &v, std::vector<Exiv2::Rational> &val) {
            for (int i = 0; i < v->count(); i++) {
                val.push_back(v->toRational(i));
            }
            return v->ok();
        }

    public:
        /*!
         * Constructor (default)
         */
        SipiExif();


        /*!
         * Constructor using an EXIF blob
         *
         * \param[in] exif Buffer containing the EXIF data
         * \Param[in] len Length of the EXIF buffer
         */
        SipiExif(const unsigned char *exif, unsigned int len);


        ~SipiExif();

        /*!
         * Returns the bytes of the EXIF data
         *
         * \param[out] len Length of buffer returned
         * \returns Buffer with EXIF data
         */
        unsigned char *exifBytes(unsigned int &len);

        /*!
         * Returns the bytes of the EXIF data as vector
         *
         * @return Vector with EXIF data
         */
        std::vector<unsigned char> exifBytes();

        /*!
         * Helper function to convert a signed float to a signed rational as used by EXIF
         *
         * \param[in] f Input signed float
         * \returns Exiv2::Rational
         */
        static Exiv2::Rational toRational(float f);

        /*!
         * Helper function to convert a unsigned float to a unsigned rational as used by EXIF
         *
         * \param[in] f Input unsigned float
         * \returns Exiv2::URational
         */
        static Exiv2::URational toURational(float f);


        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] val_p Value as string
         */
        void addKeyVal(const std::string &key_p, const std::string &val_p);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] val_p Value as string
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const std::string &val_p);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] val_p Value as float
         */
        void addKeyVal(const std::string &key_p, const float &val_p);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] vals_p Pointer to array of floats
         * \param[in] len Length of float array
         */
        void addKeyVal(const std::string &key_p, const float *vals_p, unsigned int len);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] val_p Value as float
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const float &val_p);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] vals_p Pointer to array of floats
         * \param[in] len Length of float array
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const float *vals_p, unsigned int len);


        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] val_p Value as short
         */
        void addKeyVal(const std::string &key_p, const short &val_p);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] vals_p Pointer to array of shorts
         * \param[in] len Length of short array
         */
        void addKeyVal(const std::string &key_p, const short *vals_p, unsigned int len);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] val_p Value as short
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const short &val_p);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] vals_p Pointer to array of shorts
         * \param[in] len Length of short array
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const short *vals_p, unsigned int len);


        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] val_p Value as unsigned short
         */
        void addKeyVal(const std::string &key, const unsigned short &val_p);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] vals_p Pointer to array of unsigned shorts
         * \param[in] len Length of unsigned short array
         */
        void addKeyVal(const std::string &key, const unsigned short *vals_p, int unsigned len);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] val_p Value as unsigned short
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const unsigned short &val_p);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] vals_p Pointer to array of unsigned shorts
         * \param[in] len Length of unsigned short array
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const unsigned short *vals_p, unsigned int len);


        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] val_p Value as int
         */
        void addKeyVal(const std::string &key, const int &val_p);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] vals_p Pointer to array of int
         * \param[in] len Length of int array
         */
        void addKeyVal(const std::string &key, const int *vals_p, int unsigned len);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] val_p Value as int
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const int &val_p);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] vals_p Pointer to array of ints
         * \param[in] len Length of int array
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const int *vals_p, unsigned int len);


        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] val_p Value as unsigned int
         */
        void addKeyVal(const std::string &key, const unsigned int &val_p);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] vals_p Pointer to array of unsigned int
         * \param[in] len Length of int array
         */
        void addKeyVal(const std::string &key, const unsigned int *vals_p, unsigned int len);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] val_p Value as unsigned int
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const unsigned int &val_p);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] vals_p Pointer to array of unsigned ints
         * \param[in] len Length of unsigned int array
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const unsigned int *vals_p, unsigned int len);


        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] val_p Value as Exiv2::Rational
         */
        void addKeyVal(const std::string &key, const Exiv2::Rational &r);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] vals_p Pointer to array of Exiv2::Rationals
         * \param[in] len Length of Exiv2::Rational array
         */
        void addKeyVal(const std::string &key, const Exiv2::Rational *vals_p, unsigned int len);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] val1_p Numerator of value
         * \param[in] val2_p Denominator of value
         */
        void addKeyVal(const std::string &key, const int &val1_p, const int &val2_p);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] val_p Value as Exiv2::Rational
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const Exiv2::Rational &r);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] vals_p Pointer to array of Exiv2::Rationals
         * \param[in] len Length of Exiv2::Rational array
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const Exiv2::Rational *vals_p, unsigned int len);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] val1_p Numerator of value
         * \param[in] val2_p Denominator of value
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const int &val1_p, const int &val2_p);


        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] val_p Value as Exiv2::URational
         */
        void addKeyVal(const std::string &key, const Exiv2::URational &ur);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] vals_p Pointer to array of Exiv2::URationals
         * \param[in] len Length of Exiv2::Rational array
         */
        void addKeyVal(const std::string &key, const Exiv2::URational *vals_p, unsigned int len);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] val1_p Numerator of value
         * \param[in] val2_p Denominator of value
         */
        void addKeyVal(const std::string &key, const unsigned int &val1_p, const unsigned int &val2_p);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] val_p Value as Exiv2::URational
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const Exiv2::URational &ur);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] vals_p Pointer to array of Exiv2::Rationals
         * \param[in] len Length of Exiv2::Rational array
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const Exiv2::URational *vals_p, unsigned int len);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] val1_p Numerator of value
         * \param[in] val2_p Denominator of value
         */
        void
        addKeyVal(uint16_t tag, const std::string &groupName, const unsigned int &val1_p, const unsigned int &val2_p);


        /*!
         * Add key/value pair to EXIF data
         * \param[in] tag EXIF tag number
         * \param[in] groupName EXIF tag group name
         * \param[in] vals_p Buffer of unspecified data
         * \param[in] len Length of buffer
         */
        void addKeyVal(const std::string &key_p, const unsigned char *vals_p, unsigned int len);

        /*!
         * Add key/value pair to EXIF data
         * \param[in] key_p Key as string
         * \param[in] vals_p Buffer of unspecified data
         * \param[in] len Length of buffer
         */
        void addKeyVal(uint16_t tag, const std::string &groupName, const unsigned char *vals_p, unsigned int len);

        //............................................................................
        // Getting values from the EXIF object
        //

        //____________________________________________________________________________
        // string values
        //
        template<class T>
        bool getValByKey(const std::string &key_p, T &val)  {
            try {
                Exiv2::ExifKey key = Exiv2::ExifKey(key_p);
                auto pos = exifData.findKey(key);
                if (pos == exifData.end()) {
                    return false;
                }
                auto v = pos->getValue();
                return assign_val(v, val);
            } catch (const Exiv2::BasicError<char> &err) {
                return false;
            }
        }

        template<class T>
        bool getValByKey(uint16_t tag, const std::string &groupName, T &val) {
            try {
                Exiv2::ExifKey key = Exiv2::ExifKey(tag, groupName);
                auto pos = exifData.findKey(key);
                if (pos == exifData.end()) {
                    return false;
                }
                auto v = pos->getValue();
                return assign_val(v, val);

            } catch (const Exiv2::BasicError<char> &err) {
                return false;
            }
         }

        /*
        bool getValByKey(const std::string &key_p, std::string &str_p);

        bool getValByKey(uint16_t tag, const std::string &groupName, std::string &str_p);

        bool getValByKey(uint16_t tag, const std::string &groupName, std::vector<std::string> &str_p);


        //____________________________________________________________________________
        // unsigned char values
        //
        bool getValByKey(const std::string &key_p, char &c);

        bool getValByKey(const std::string &key_p, std::vector<char> &vc);

        bool getValByKey(uint16_t tag, const std::string &groupName, char &c);

        bool getValByKey(uint16_t tag, const std::string &groupName, std::vector<char> &vc);

        //____________________________________________________________________________
        // unsigned char values
        //
        bool getValByKey(const std::string &key_p, unsigned char &uc);

        bool getValByKey(const std::string &key_p, std::vector<unsigned char> &vuc);

        bool getValByKey(uint16_t tag, const std::string &groupName, unsigned char &uc);

        bool getValByKey(uint16_t tag, const std::string &groupName, std::vector<unsigned char> &vuc);


        //____________________________________________________________________________
        // float values
        //
        bool getValByKey(const std::string &key_p, float &val_p);

        bool getValByKey(const std::string &key_p, std::vector<float> &f);
        bool getValByKey(uint16_t tag, const std::string &groupName, float &val_p);

        bool getValByKey(uint16_t tag, const std::string &groupName, std::vector<float> &val_p);


        //____________________________________________________________________________
        // Rational values
        //
        bool getValByKey(const std::string &key_p, Exiv2::Rational &r);

        bool getValByKey(const std::string &key_p, std::vector<Exiv2::Rational> &rv);

        bool getValByKey(uint16_t tag, const std::string &groupName, Exiv2::Rational &r);

        bool getValByKey(const std::string &key_p, std::vector<Exiv2::Rational> &rv);

        bool getValByKey(uint16_t tag, const std::string &groupName, std::vector<Exiv2::Rational> &r);


        //____________________________________________________________________________
        // short values
        //
        bool getValByKey(const std::string &key_p, short &s);

        bool getValByKey(const std::string &key_p, std::vector<short> &sv);

        bool getValByKey(uint16_t tag, const std::string &groupName, short &s);

        bool getValByKey(uint16_t tag, const std::string &groupName, std::vector<short> &s);


        //____________________________________________________________________________
        // unsigned short values
        //
        bool getValByKey(const std::string &key_p, unsigned short &s);

        bool getValByKey(const std::string &key_p, std::vector<unsigned short> &s);

        bool getValByKey(uint16_t tag, const std::string &groupName, unsigned short &s);

        bool getValByKey(uint16_t tag, const std::string &groupName, std::vector<unsigned short> &s);


        //____________________________________________________________________________
        // int values
        //
        bool getValByKey(const std::string &key_p, int &s);

        bool getValByKey(const std::string &key_p, std::vector<int> &sv);

        bool getValByKey(uint16_t tag, const std::string &groupName, int &s);

        bool getValByKey(uint16_t tag, const std::string &groupName, std::vector<int> &s);


        //____________________________________________________________________________
        // unsigned int values
        //
        bool getValByKey(const std::string &key_p, unsigned int &s);

        bool getValByKey(const std::string &key_p, std::vector<unsigned int> &sv);
        bool getValByKey(uint16_t tag, const std::string &groupName, unsigned int &s);

        bool getValByKey(uint16_t tag, const std::string &groupName, std::vector<unsigned int> &s);
        */

        friend std::ostream &operator<<(std::ostream &lhs, SipiExif &rhs);

    };

}

#endif
