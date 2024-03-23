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
class SipiError : public shttps::Error
{
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

}// namespace Sipi

#endif
