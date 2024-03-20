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
#include <cstdlib>
#include <cstring>
#include <sstream>


#include "SipiError.hpp"

namespace Sipi {

    SipiError::SipiError(const char *file_p, const int line_p, const char *msg, int errno_p) : Error(file_p, line_p,
                                                                                                     msg, errno_p) {}
    //============================================================================


    SipiError::SipiError(const char *file_p, const int line_p, const std::string &msg, int errno_p) : Error(file_p,
                                                                                                            line_p, msg,
                                                                                                            errno_p) {}
    //============================================================================

    std::string SipiError::to_string() const {
        std::ostringstream errStream;
        errStream << "Sipi Error at [" << file << ": " << line << "]";
        if (sysErrno != 0) errStream << " (system error: " << std::strerror(sysErrno) << ")";
        errStream << ": " << message;
        return errStream.str();
    }
    //============================================================================

    std::ostream &operator<<(std::ostream &outStream, const SipiError &rhs) {
        const std::string errStr = rhs.to_string();
        outStream << errStr << std::endl; // TODO: remove the endl, the logging code should do it
        return outStream;
    }
    //============================================================================


}
