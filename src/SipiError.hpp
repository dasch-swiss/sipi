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

#ifndef __defined_sipierror_h
#define __defined_sipierror_h

/*!
 * \brief Exception class for the Sipi packge
 *
 * All methods within the Sipi package my throw an exception of the
 * type SipiError which usually contains information about the source file,
 * the line number and a description of what went wrong.
 */

#include <string>
#include <memory>

#include "../shttps/Error.h"

/**
 * \namespace Sipi Used for all sipi things.
 */
namespace Sipi {

    /*!
     * \class SipiError "SipiError.hpp"
     * \brief Class that implements the error handling with exceptions
     *
     * Used for giving error messages while throwing
     * an exception. The class inherits from std::runtime_error.
     */
    class SipiError : public shttps::Error {
    private:
    public:

        /*!
         * Constructor with all (char *) strings
         *
         * \param[in] file The filename, usually the __FILE__ macro.
         * \param[in] line The source code line, usually the __LINE__ macro.
         * \param[in] msg The message describing the error.
         * \param[in] errno_p Retrieve and display the system error message from errno.
         */
        SipiError(const char *file, const int line, const char *msg, int errno_p = 0);

        /*!
         * Constructor with std::string strings for the message. The file parameter is
         * is always of type (char *), becuase usually its either __LINE__ or a static
         * pointer to char
         *
         * \param[in] file The filename, usually the __FILE__ macro.
         * \param[in] line The source code line, usually the __LINE__ macro.
         * \param[in] msg The message describing the error.
         * \param[in] syserr Retrieve and display the system error message from errno.
         */
        SipiError(const char *file, const int line, const std::string &msg, int errno_p = 0);

        /*!
        * Convert the error into a string message
        */
        std::string to_string() const;

        /*!
         * The overloaded << operator which is used to write the error message to the output
         *
         * \param[in] lhs The output stream
         * \param[in] rhs Reference to an instance of a SipiError
         * \returns Returns an std::ostream object
         */
        friend std::ostream &operator<<(std::ostream &lhs, const SipiError &rhs);

    };

}

#endif
