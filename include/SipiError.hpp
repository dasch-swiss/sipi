/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
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

#include <memory>
#include <string>

#include "shttps/Error.h"

/**
 * \namespace Sipi Used for all sipi things.
 */
namespace Sipi {

/*!
 * \class SipiError "SipiError.hpp"
 * \brief Class that implements the error handling with exceptions
 *
 * Used for giving error messages while throwing
 * an exception.
 */
class SipiError : public shttps::Error
{
private:
public:
  /*!
   * Constructor with all (char *) strings
   *
   * \param[in] msg The message describing the error.
   * \param[in] errno_p Retrieve and display the system error message from errno.
   * \param[in] loc The source location where the error occurs
   */
  explicit SipiError(const char *msg, int errno_p = 0, const std::source_location &loc = std::source_location::current());

  /*!
   * Constructor with std::string strings for the message. The file parameter is
   * is always of type (char *), becuase usually its either __LINE__ or a static
   * pointer to char
   *
   * \param[in] msg The message describing the error.
   * \param[in] errno_p Retrieve and display the system error message from errno.
   */
  explicit SipiError(const std::string &msg, int errno_p = 0, const std::source_location &loc = std::source_location::current());

  /*!
   * The overloaded << operator which is used to write the error message to the output
   *
   * \param[in] lhs The output stream
   * \param[in] rhs Reference to an instance of a SipiError
   * \returns Returns an std::ostream object
   */
  friend std::ostream &operator<<(std::ostream &lhs, const SipiError &rhs);
};

}// namespace Sipi

#endif
