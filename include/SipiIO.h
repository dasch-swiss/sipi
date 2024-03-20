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
/*!
 * This file implements the virtual abstract class which implements the image file I/O.
 */
#ifndef __sipi_io_h
#define __sipi_io_h

#include <unordered_map>
#include <string>
#include <stdexcept>

#include "../src/SipiImage.hpp"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiSize.h"

/**
 * @namespace Sipi Is used for all Sipi things.
 */
namespace Sipi {

    typedef enum {
        HIGH = 0, MEDIUM = 1, LOW = 2
    } ScalingMethod;

    typedef struct _ScalingQuality {
        ScalingMethod jk2;
        ScalingMethod jpeg;
        ScalingMethod tiff;
        ScalingMethod png;
    } ScalingQuality;

    typedef enum : unsigned short { // from the TIFF specification...
        TOPLEFT = 1,  //!< The 0th row represents the visual top of the image, and the 0th column represents the visual left-hand side.
        TOPRIGHT = 2, //!< The 0th row represents the visual top of the image, and the 0th column represents the visual right-hand side.
        BOTRIGHT = 3, //!< The 0th row represents the visual bottom of the image, and the 0th column represents the visual right-hand side.
        BOTLEFT = 4,  //!< The 0th row represents the visual bottom of the image, and the 0th column represents the visual left-hand side.
        LEFTTOP = 5,  //!< The 0th row represents the visual left-hand side of the image, and the 0th column represents the visual top.
        RIGHTTOP = 6, //!< The 0th row represents the visual right-hand side of the image, and the 0th column represents the visual top.
        RIGHTBOT = 7, //!< The 0th row represents the visual right-hand side of the image, and the 0th column represents the visual bottom.
        LEFTBOT = 8   //!< The 0th row represents the visual left-hand side of the image, and the 0th column represents the visual bottom.
    } Orientation;


    class SipiImgInfo {
    public:
        enum { FAILURE = 0, DIMS = 1, ALL = 2 } success;
        int width;
        int height;
        Orientation orientation;
        int tile_width;
        int tile_height;
        int clevels;
        int numpages;
        std::string internalmimetype;
        std::string origname;
        std::string origmimetype;

        SipiImgInfo() : success(FAILURE), width(0), height(0), orientation(TOPLEFT), tile_height(0), clevels(0), numpages(0) {};
    };

    enum {
        JPEG_QUALITY,
        J2K_Sprofile,
        J2K_Creversible,
        J2K_Clayers,
        J2K_Clevels,
        J2K_Corder,
        J2K_Cprecincts,
        J2K_Cblk,
        J2K_Cuse_sop,
        J2K_Stiles,
        J2K_rates
    } SipiCompressionParamName;

    typedef std::unordered_map<int, std::string> SipiCompressionParams;

    class SipiImage; //!< forward declaration of class SipiImage

    /*!
     * This is the virtual base class for all classes implementing image I/O.
     */
    class SipiIO {
    public:
        virtual ~SipiIO() = default;;


        /*!
         * Method used to read an image file
         *
         * \param img Pointer to SipiImage instance
         * \param filepath Image file path
         * \param region Region of the image to read
         * \param size Size of the image to read
         * \param force_bps_8 Convert the file to 8 bits/sample on reading thus enforcing an 8 bit image
         * \param scaling_quality Quality of the scaling algorithm
         */
        virtual bool read(SipiImage *img, const std::string &filepath, std::shared_ptr<SipiRegion> region,
                          std::shared_ptr<SipiSize> size, bool force_bps_8,
                          ScalingQuality scaling_quality) = 0;

        bool read(SipiImage *img, const std::string &filepath) {
            return read(img, filepath, nullptr, nullptr, false,
                        {HIGH, HIGH, HIGH, HIGH});
        }

        bool read(SipiImage *img, const std::string &filepath, const std::shared_ptr<SipiRegion> region) {
            return read(img, filepath, region, nullptr, false,
                        {HIGH, HIGH, HIGH, HIGH});
        }

        bool read(SipiImage *img, const std::string &filepath, const std::shared_ptr<SipiRegion> region,
                  const std::shared_ptr<SipiSize> size) {
            return read(img, filepath, region, size, false,
                        {HIGH, HIGH, HIGH, HIGH});
        }

        bool read(SipiImage *img, const std::string &filepath, const std::shared_ptr<SipiRegion> region,
                  std::shared_ptr<SipiSize> size, bool force_bps_8) {
            return read(img, filepath, region, size, force_bps_8,
                 {HIGH, HIGH, HIGH, HIGH});
        }

        /*!
         * Get the dimension of the image
         *
         * \param filepath Pathname of the image file
         */
        virtual SipiImgInfo getDim(const std::string &filepath) = 0;


        /*!
         * Write an image for a file using the given file format implemented by the subclass
         *
         * \param img Pointer to SipiImage instance
         * \param filepath Name of the image file to be written. Please note that
         * - "-" means to write the image data to stdout
         * - "HTTP" means to write the image data to the HTTP-server output
         * \param params Compression parameters
         */
        virtual void write(SipiImage *img, const std::string &filepath, const SipiCompressionParams *params) = 0;

        void write(SipiImage *img, const std::string &filepath) {
            write(img, filepath, nullptr);
        }
    };

}

#endif
