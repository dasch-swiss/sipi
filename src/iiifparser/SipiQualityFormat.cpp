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
#include <assert.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>

#include <stdio.h>
#include <string.h>

#include "../SipiError.hpp"
#include "SipiQualityFormat.h"


#include <string>

static const char __file__[] = __FILE__;

namespace Sipi {

    SipiQualityFormat::SipiQualityFormat(std::string str) {
        if (str.empty()) {
            quality_type = SipiQualityFormat::DEFAULT;
            format_type = SipiQualityFormat::JPG;
            return;
        }

        size_t dot_pos = str.find(".");

        if (dot_pos == std::string::npos) {
            throw SipiError(__file__, __LINE__, "IIIF Error reading Quality+Format parameter  \"" + str + "\" !");
        }

        std::string quality = str.substr(0, dot_pos);
        std::string format = str.substr(dot_pos + 1);

        if (quality == "default") {
            quality_type = SipiQualityFormat::DEFAULT;
        } else if (quality == "color") {
            quality_type = SipiQualityFormat::COLOR;
        } else if (quality == "gray") {
            quality_type = SipiQualityFormat::GRAY;
        } else if (quality == "bitonal") {
            quality_type = SipiQualityFormat::BITONAL;
        } else {
            throw SipiError(__file__, __LINE__, "IIIF Error reading Quality parameter  \"" + quality + "\" !");
        }

        if (format == "jpg") {
            format_type = SipiQualityFormat::JPG;
        } else if (format == "tif") {
            format_type = SipiQualityFormat::TIF;
        } else if (format == "png") {
            format_type = SipiQualityFormat::PNG;
        } else if (format == "gif") {
            format_type = SipiQualityFormat::GIF;
        } else if (format == "jp2") {
            format_type = SipiQualityFormat::JP2;
        } else if (format == "pdf") {
            format_type = SipiQualityFormat::PDF;
        } else if (format == "webp") {
            format_type = SipiQualityFormat::WEBP;
        } else {
            format_type = SipiQualityFormat::UNSUPPORTED;
        }
    }


    //-------------------------------------------------------------------------
    // Output to stdout for debugging etc.
    //
    std::ostream &operator<<(std::ostream &outstr, const SipiQualityFormat &rhs) {
        outstr << "IIIF-Server QualityFormat parameter: ";
        outstr << "  Quality: " << rhs.quality_type;
        outstr << " | Format: " << rhs.format_type;
        return outstr;
    }
    //-------------------------------------------------------------------------

}
