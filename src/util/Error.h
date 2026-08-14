/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * \brief Implements an error class for the http server.
 */
#ifndef _defined_shttps_error_h
#define _defined_shttps_error_h

#include <iostream>
#include <source_location>
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


public:
  [[nodiscard]] unsigned int getLine() const { return location.line(); }

  [[nodiscard]] std::string getFile() const { return location.file_name(); }

  [[nodiscard]] std::string getMessage() const { return message; }

  [[nodiscard]] int getSysErrno() const { return sysErrno; }

  /*!
   * Constructor with all (char *) strings
   *
   * \param[in] msg The message describing the error.
   * \param[in] errno_p Retrieve and display the system error message from errno.
   * \param[in] loc The source location where the error occurs
   */
  explicit Error(const char *msg, int errno_p = 0, const std::source_location &loc = std::source_location::current());

  /*!
   * Constructor with std::string strings for the message.
   *
   * \param[in] msg The message describing the error.
   * \param[in] errno_p Retrieve and display the system error message from errno.
   * \param[in] loc The source location where the error occurs
   */
  explicit Error(const std::string &msg, int errno_p = 0, const std::source_location &loc = std::source_location::current());

  /*!
   * Retuns the error as a one-line string
   *
   * \returns Error string
   */
  [[nodiscard]] virtual std::string to_string() const;

  /*!
   * The overloaded << operator which is used to write the error message to the output
   *
   * \param[in] out_stream The output stream
   * \param[in] rhs Reference to an instance of a Error
   * \returns Returns an std::ostream object
   */
  friend std::ostream &operator<<(std::ostream &out_stream, const Error &rhs);

private:
  std::string message;//!< Description of the problem
  int sysErrno;//!< If there is a system error number
  std::source_location location;//!< The source location where the error occurs
};
}// namespace shttps

#endif
