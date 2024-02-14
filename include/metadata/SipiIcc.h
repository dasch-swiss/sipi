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
 * This file implements the Handling of ICC color profiles. It makes heavy use of the functions
 * provided by the littleCMS2 library (see http://www.littlecms.com )
 */
#ifndef __sipi_icc_h
#define __sipi_icc_h

#include <string>
#include <vector>

#include <stdio.h>
#include <limits.h>
#include <time.h>
#include <stddef.h>

#include <lcms2.h>


namespace Sipi {

    extern void icc_error_logger(cmsContext ContextID, cmsUInt32Number ErrorCode, const char *Text);

    class SipiImage; //!< forward declaration

    /*! Defines predefined profiles which are used by SipiIcc */
    typedef enum {
        icc_undefined,      //!< No profile data defined, empty profile
        icc_unknown,        //!< This is a unknown, proprietary profile, probably embedded
        icc_sRGB,           //!< Standard sRGB profile
        icc_AdobeRGB,       //!< Standard AdobeRGB profile
        icc_RGB,            //!< A RGB profile that's given with parameters such as white point, primary colors etc. (see TIFF specs)
        icc_CMYK_standard,  //!< A "standard" CMYK profile. We currently use the "USWebCoatedSWOP" profile
        icc_GRAY_D50,       //!< A standard profile for gray value images using a D50 light source and a gamma of 2.2
        icc_LUM_D65,        //!< A standard profile for gray value images as used be JPEG2000 JP2_sLUM_SPACE
        icc_ROMM_GRAY,       //!< A profile used by the JPEG2000 ISO suite....
        icc_LAB
    } PredefinedProfiles;

    /*!
     * This class implements the handling of ICC color profiles
     */
    class SipiIcc {
    private:
        cmsHPROFILE icc_profile{};            //!< Handle of the littleCMS profile data
        PredefinedProfiles profile_type;    //!< Profile type that is represented

    public:
        /*!
         * Constructor (default) which results in empty, undefined profile
         */
        inline SipiIcc() {
            icc_profile = NULL;
            profile_type = icc_undefined;
        };

        /*!
         * Constructor which takes a blob that contains the ICC profile
         * \param[in] buf Buffer holding the binary profile data
         * \param]in len Length of the buffer
         */
        SipiIcc(const unsigned char *buf, int len);

        /*!
         * Copy constructor. Uses deep copy and allocates new data buffers.
         * \param[in] icc_p Profile that acts as template for the new profile.
         */
        SipiIcc(const SipiIcc &icc_p);

        /*!
         * Constructor using littleCMS profile
         * \param[in] icc_profile_p LittleCMS profile
         */
        explicit SipiIcc(cmsHPROFILE &icc_profile_p);

        /**
         * Constructor to create a predefined profile
         * \param[in] predef Desired predefined profile
         */
        explicit SipiIcc(PredefinedProfiles predef);

        /**
         * Constructor of an ICC RGB profile using white point, primaries and transfer function (if available),
         * otherwise a curve with gamma = 2.2 is assumed. The parameters must be given as retrieved by libtiff!
         * \param[in] white_point Array of 2 floats giving the x and y of the whitepoint
         * \param[in] primaries Array of 6 floats giving the x and y of each red, green and blue primaries
         * \param[in] tfunc Transfer function tables as retrieved by libtiff with either (1 << bitspersample) or 3*(1 << bitspersample) entries
         * \param[in] Length of tranfer function table
         */
        SipiIcc(float white_point_p[], float primaries_p[], const unsigned short *tfunc = nullptr, int tfunc_len = 0);

        /**
         * Destructor
         */
        ~SipiIcc();

        /*!
         * Assignment operator which makes deep copy
         * \param[in] rhs Instance of SipiIcc
         */
        SipiIcc &operator=(const SipiIcc &rhs);

        /*!
         * Get the blob containing the ICC profile
         * @param[out] len Length of the buffer returned
         * @returns Buffer containing the binary ICC profile
         */
        unsigned char *iccBytes(unsigned int &len);

        /*!
         * Get the blob containing the ICC profile as std::vector
         * @return std:vector containing the binary ICC profile
         */
        std::vector<unsigned char> iccBytes();

        /*!
         * Retireve the littleCMS profile
         * \returns Handle to littleCMS profile
         */
        cmsHPROFILE getIccProfile() const;

        /*!
         * Get the profile type
         * \retruns Gives the predefined profile type
         */
        inline PredefinedProfiles getProfileType() const { return profile_type; };

        /*!
         * returns a littleCMS formatter with the given bits/sample
         * \param[in] bps Desired bits/sample
         * \returns Formatter as used by cmsTransfrom
         */
        unsigned int iccFormatter(int bps) const;

        /*!
         * returns a littleCMS formatter with the given bits/sample for the given SipImage
         * \param img The SipiImage instance whose color profile is used to create the formatter
         * \returns Formatter as used by cmsTransfrom
         */
        unsigned int iccFormatter(SipiImage *img) const;

        /**
         * Print info to output stream
         * \param[in] lhs Output stream
         * \param[in] rhs SipiIcc instance
         */
        friend std::ostream &operator<<(std::ostream &lhs, SipiIcc &rhs);
    };

}

#endif
