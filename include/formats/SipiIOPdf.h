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
#ifndef SIPI_SIPIIOPDF_H
#define SIPI_SIPIIOPDF_H
#include <string>

#include "SipiImage.h"
#include "SipiIO.h"

namespace Sipi {
    class SipiIOPdf : public SipiIO {
    private:

    public:
        ~SipiIOPdf() override = default;

        bool read(SipiImage *img, const std::string &filepath, int pagenum, std::shared_ptr<SipiRegion> region,
                  std::shared_ptr<SipiSize> size, bool force_bps_8, ScalingQuality scaling_quality) override;

        SipiImgInfo getDim(const std::string &filepath, int pagenum) override;

        void write(SipiImage *img, const std::string &filepath, const SipiCompressionParams *params) override;

    };
}

#endif //SIPI_SIPIIOPDF_H
