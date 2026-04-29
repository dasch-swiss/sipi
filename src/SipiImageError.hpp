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
 * Base class for image-format errors. Subclass this (not `final` — intentional)
 * to model distinct failure modes as types. The HTTP handler and other catch
 * sites dispatch by type, so a dedicated subclass is the cheapest way to give
 * an error a different policy (Sentry capture, HTTP status, client message).
 *
 * Pattern for new subclasses:
 *   - Inherit publicly from `SipiImageError`
 *   - `using SipiImageError::SipiImageError;` to forward constructors (leaf types
 *     with no extra state); define explicit constructors if carrying a payload
 *   - Mark the subclass `final` unless it is itself a further base
 *   - Place the new `catch (MyError &)` BEFORE `catch (SipiImageError &)` at
 *     every call site (derived-most first)
 *
 * See `docs/src/development/error-model.md` for the full catalog and policy
 * mapping (HTTP status, Sentry capture, client-facing message).
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

/*!
 * Signals that a write to an HTTP response failed because the peer is gone
 * (FIN, RST, or write timeout). Detected at the write site by catching
 * `shttps::OUTPUT_WRITE_FAIL` — the only thing `shttps::Connection`'s
 * `sendAndFlush` / `flush` raises on a socket-write failure. Every such
 * throw is a definitive abort, so no further `peerConnected()` check is
 * needed (POLLRDHUP misses RST and write-timeout aborts). The HTTP handler
 * uses the distinct type to skip Sentry capture — these are not
 * server-side errors.
 */
class SipiImageClientAbortError final : public SipiImageError
{
public:
  using SipiImageError::SipiImageError;
};
}// namespace Sipi

#endif// SIPIIMAGEERROR_HPP
