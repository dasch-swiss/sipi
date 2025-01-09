/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPIIMAGEERROR_HPP
#define SIPIIMAGEERROR_HPP

#include <cstring>
#include <source_location>
#include <sstream>

namespace Sipi {

/*!
 * This class implements the error handling for the different image formats.
 */
class SipiImageError final : public std::exception
{

public:
  bool fatal = false;
  SipiImageError &setFatal(bool fatal_)
  {
    fatal = fatal_;
    return *this;
  }

  /*!
   * Constructor
   * \param[in] errnum_p if a unix system call is the reason for throwing this exception
   * \param[in] loc The source location where the error occurs
   */
  explicit SipiImageError(const int errnum_p = 0, const std::source_location &loc = std::source_location::current())
    : errnum_{ errnum_p }, location_{ loc }
  {
    std::ostringstream errStream;
    errStream << "Sipi image error at [" << location_.file_name() << ": " << location_.line() << "]";
    if (errnum_ != 0) { errStream << " (system error: " << std::strerror(errnum_) << ")"; }
    errStream << ": " << errmsg_;
    fullerrmsg_ = errStream.str();
  }

  /*!
   * Constructor
   * \param[in] msg_p Error message describing the problem
   * \param[in] errnum_p Errnum, if a unix system call is the reason for throwing this exception
   * \param[in] loc The source location where the error occurs
   */
  explicit SipiImageError(const char *msg_p,
    const int errnum_p = 0,
    const std::source_location &loc = std::source_location::current())
    : errmsg_{ msg_p }, errnum_{ errnum_p }, location_{ loc }
  {
    std::ostringstream errStream;
    errStream << "Sipi image error at [" << location_.file_name() << ": " << location_.line() << "]";
    if (errnum_ != 0) { errStream << " (system error: " << std::strerror(errnum_) << ")"; }
    errStream << ": " << errmsg_;
    fullerrmsg_ = errStream.str();
  }

  /*!
   * Constructor
   * \param[in] msg_p Error message describing the problem
   * \param[in] errnum_p Errnum, if a unix system call is the reason for throwing this exception
   */
  SipiImageError(std::string msg_p,
    const int errnum_p = 0,
    const std::source_location &loc = std::source_location::current())
    : errmsg_{ std::move(msg_p) }, errnum_{ errnum_p }, location_{ loc }
  {
    std::ostringstream errStream;
    errStream << "Sipi image error at [" << location_.file_name() << ": " << location_.line() << "]";
    if (errnum_ != 0) { errStream << " (system error: " << std::strerror(errnum_) << ")"; }
    errStream << ": " << errmsg_;
    fullerrmsg_ = errStream.str();
  }

  [[nodiscard]] std::string to_string() const
  {
    std::ostringstream errStream;
    errStream << "Sipi image error at [" << location_.file_name() << ": " << location_.line() << "]";
    if (errnum_ != 0) { errStream << " (system error: " << std::strerror(errnum_) << ")"; }
    errStream << ": " << errmsg_;
    return errStream.str();
  }
  //============================================================================

  [[nodiscard]] const char *what() const noexcept override { return fullerrmsg_.c_str(); }

  friend std::ostream &operator<<(std::ostream &outStream, const SipiImageError &rhs)
  {
    const std::string errStr = rhs.to_string();
    outStream << errStr << '\n';
    return outStream;
  }

private:
  std::string errmsg_;//!< Error message
  int errnum_;//!< error number if a system call is the reason for the error
  std::source_location location_;//!< Source location where the error occurs
  std::string fullerrmsg_;
};
}// namespace Sipi

#endif// SIPIIMAGEERROR_HPP
