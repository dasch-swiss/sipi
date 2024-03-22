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
#include "SipiRotation.h"
#include "shttps/Parsing.h"

static const char __file__[] = __FILE__;

namespace Sipi {

    SipiRotation::SipiRotation() {
        mirror = false;
        rotation = 0.F;
        return;
    }

    SipiRotation::SipiRotation(std::string str) {
        try {
            if (str.empty()) {
                mirror = false;
                rotation = 0.F;
                return;
            }

            mirror = str[0] == '!';
            if (mirror) {
                str.erase(0, 1);
            }

            rotation = shttps::Parsing::parse_float(str);
        } catch (shttps::Error &error) {
            throw SipiError(__file__, __LINE__, "Could not parse IIIF rotation parameter: " + str);
        }
    }
    //-------------------------------------------------------------------------


    //-------------------------------------------------------------------------
    // Output to stdout for debugging etc.
    //
    std::ostream &operator<<(std::ostream &outstr, const SipiRotation &rhs) {
        outstr << "IIIF-Server Rotation parameter:";
        outstr << "  Mirror " << rhs.mirror;
        outstr << " | rotation = " << std::to_string(rhs.rotation);
        return outstr;
    }
    //-------------------------------------------------------------------------
}
