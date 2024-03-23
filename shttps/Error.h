/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * \brief Implements an error class for the http server.
 */
#ifndef __defined_shttps_error_h
#define __defined_shttps_error_h

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace shttps {

/*!
 * \brief Class used to thow errors from the web server implementation
 *
 * This class which inherits from \class std::runtime_error is used to throw catchable
 * errors from the web server. The error contains the cpp-file, line number, a user given
 * description and, if available, the system error message.
 */
class Error : public std::runtime_error
{
protected:
  int line;//!< Linenumber where the exception has been throwns
  std::string file;//!< Name of source code file where the exception has been thrown
  std::string message;//!< Description of the problem
  int sysErrno;//!< If there is a system error number

public:
  inline int getLine(void) const { return line; }

  inline std::string getFile(void) const { return file; }

  inline std::string getMessage(void) const { return message; }

  inline int getSysErrno(void) const { return sysErrno; }

  /*!
   * Constructor with all (char *) strings
   *
   * \param[in] file The filename, usually the __FILE__ macro.
   * \param[in] line The source code line, usually the __LINE__ macro.
   * \param[in] msg The message describing the error.
   * \param[in] errno_p Retrieve and display the system error message from errno.
   */
  Error(const char *file, const int line, const char *msg, int errno_p = 0);

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
  Error(const char *file, const int line, const std::string &msg, int errno_p = 0);

  /*!
   * Retuns the error as a one-line string
   *
   * \returns Error string
   */
  std::string to_string(void) const;

  /*!
   * The overloaded << operator which is used to write the error message to the output
   *
   * \param[in] outStream The output stream
   * \param[in] rhs Reference to an instance of a Error
   * \returns Returns an std::ostream object
   */
  friend std::ostream &operator<<(std::ostream &outStream, const Error &rhs);
};
}// namespace shttps

#endif
