/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPIIMAGEERROR_HPP
#define SIPIIMAGEERROR_HPP

#include <cstring>
#include <source_location>
#include <sstream>

#include "util/PathRedact.h"

namespace Sipi {

/*!
 * Exception type for the unrecoverable / invariant class of image errors:
 * allocation-size overflow (`checked_buf_size_or_throw`), `memTiffOpen`'s raw
 * `malloc` failures, geometry invariants a caller violated, and out-of-range
 * pixel accessors (`SipiImage::getPixel`/`setPixel`). These are programming
 * errors with no input path that reaches them — never a fallible operation
 * on file, network, or request-derived data.
 *
 * A fallible operation (decode, encode, metadata parse, IIIF transform)
 * returns `Result<T>` (`std::expected<T, SipiValueError>`) instead of
 * throwing. Policy for a `SipiValueError` (HTTP status, Sentry capture,
 * client-facing message) is data, not type: see `policy_for(ErrorCode)` in
 * `src/error/cpp/SipiValueError.h`. Do not subclass `SipiImageError` to model
 * policy — there is no type-ordered `catch` chain to dispatch on.
 *
 * See `docs/src/development/error-model.md` and
 * `docs/adr/0024-value-based-image-errors.md` for the full error-handling
 * contract.
 */
class SipiImageError : public std::exception
{

public:
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

  /*!
   * Client-safe error message: the human-readable description (and system
   * error text, if any) WITHOUT the source location. Suitable for HTTP
   * response bodies / script-visible strings; `to_string()`/`what()` carry
   * the full diagnostic (source file + line) for server-side logs.
   */
  [[nodiscard]] std::string message() const
  {
    std::ostringstream errStream;
    if (errnum_ != 0) { errStream << "(system error: " << std::strerror(errnum_) << "): "; }
    errStream << errmsg_;
    return redact_paths(errStream.str());
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
