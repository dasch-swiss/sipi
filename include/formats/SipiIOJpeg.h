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
 * This file handles the reading and writing of JPEG 2000 files using libtiff.
 */
#ifndef __sipi_io_jpeg_h
#define __sipi_io_jpeg_h

#include <string>

#include "SipiImage.h"
#include "SipiIO.h"

namespace Sipi {

    /*! Class which implements the JPEG2000-reader/writer */
    class SipiIOJpeg : public SipiIO {
    private:
        static void parse_photoshop(SipiImage *img, char *data, int length);

    public:
        ~SipiIOJpeg() override = default;;

        /*!
         * Method used to read an image file
         *
         * \param *img Pointer to SipiImage instance
         * \param filepath Image file path
         * \param reduce Reducing factor. If it is not 0, the reader
         * only reads part of the data returning an image with reduces resolution.
         * If the value is 1, only half the resolution is returned. If it is 2, only one forth etc.
         */
        bool read(SipiImage *img, const std::string &filepath, std::shared_ptr<SipiRegion> region,
                  std::shared_ptr<SipiSize> size, bool force_bps_8, ScalingQuality scaling_quality) override;

        /*!
         * Get the dimension of the image
         *
         * \param[in] filepath Pathname of the image file
         * \param[out] width Width of the image in pixels
         * \param[out] height Height of the image in pixels
         */
        Sipi::SipiImgInfo getDim(const std::string &filepath) override;


        /*!
         * Write a JPEG image to a file, stdout or to a memory buffer
         *
         * \param *img Pointer to SipiImage instance
         * \param filepath Name of the image file to be written.
         */
        void write(SipiImage *img, const std::string &filepath, const SipiCompressionParams *params) override;
    };

}


#endif
