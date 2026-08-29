/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPIVALUEERROR_HPP
#define SIPIVALUEERROR_HPP

#include <cstring>
#include <expected>
#include <source_location>
#include <sstream>
#include <string>
#include <utility>

#include "util/PathRedact.h"

namespace Sipi {

/*!
 * One variant per distinct failure category the image/codec layer's fallible
 * operations can report, per docs/adr/0024-value-based-image-errors.md and
 * docs/src/development/error-model.md (the living catalog; that table is
 * normative for this list).
 *
 * `std::bad_alloc` is deliberately NOT a variant here (ADR-0024 Decision 7):
 * resource exhaustion is a systemic condition, not malformed input, and stays
 * exception-based permanently. Do not add it.
 */
enum class ErrorCode {
  kDecodeFailed,
  kUnsupportedFormat,
  kMalformedInput,
  kWriteFailed,
  kClientAbort,
  kShapeProbeFailed,
  kMetadataParseFailed,
  kInvalidRequestParameter,
};

/*!
 * Most `ErrorCode` variants map to `kInternalError` (error-model.md's policy
 * table); `kInvalidRequestParameter` maps to `kClientError`. The enum exists
 * so each seam translates the class into its own vocabulary (an HTTP status
 * on the HTTP seam, a Lua-visible string on the scripting seam, an exit code
 * on the CLI seam) rather than special-casing a raw `ErrorCode` check at a
 * seam.
 */
enum class HttpStatusClass { kInternalError, kClientError };

enum class SentryPolicy { kReport, kSkip };

/*!
 * `MetricHint::kNone` is not itself listed in error-model.md's table (rows
 * with "none" mean no metric is incremented) but is defined here so
 * `ErrorPolicy::metric_hint` is a total field rather than an
 * `std::optional<MetricHint>`.
 */
enum class MetricHint { kNone, kClientDisconnected };

struct ErrorPolicy
{
  HttpStatusClass http_status_class;
  SentryPolicy sentry_policy;
  MetricHint metric_hint;
};

/*!
 * Total over `ErrorCode`: every variant returns a policy. A `switch` with no
 * `default` means a new `ErrorCode` added without a corresponding case fails
 * to compile under `-Wswitch`; `sipi_value_error_test.cpp` additionally
 * asserts the exact values documented in error-model.md's policy table.
 */
[[nodiscard]] constexpr ErrorPolicy policy_for(ErrorCode code)
{
  switch (code) {
  case ErrorCode::kDecodeFailed:
    return { HttpStatusClass::kInternalError, SentryPolicy::kReport, MetricHint::kNone };
  case ErrorCode::kUnsupportedFormat:
    return { HttpStatusClass::kInternalError, SentryPolicy::kReport, MetricHint::kNone };
  case ErrorCode::kMalformedInput:
    return { HttpStatusClass::kInternalError, SentryPolicy::kReport, MetricHint::kNone };
  case ErrorCode::kWriteFailed:
    return { HttpStatusClass::kInternalError, SentryPolicy::kReport, MetricHint::kNone };
  case ErrorCode::kClientAbort:
    return { HttpStatusClass::kInternalError, SentryPolicy::kSkip, MetricHint::kClientDisconnected };
  case ErrorCode::kShapeProbeFailed:
    return { HttpStatusClass::kInternalError, SentryPolicy::kReport, MetricHint::kNone };
  case ErrorCode::kMetadataParseFailed:
    return { HttpStatusClass::kInternalError, SentryPolicy::kReport, MetricHint::kNone };
  case ErrorCode::kInvalidRequestParameter:
    return { HttpStatusClass::kClientError, SentryPolicy::kSkip, MetricHint::kNone };
  }
}

/*!
 * Value-returning replacement for `SipiImageError` (ADR-0024). Carries an
 * `ErrorCode` discriminant, a diagnostic message, and the `std::source_location`
 * captured at construction. Cheap to move: no eager formatting happens in any
 * constructor, unlike `SipiImageError`'s `fullerrmsg_`; the diagnostic string
 * is assembled lazily in `diagnostic_message()`. A value, never thrown: no
 * inheritance from `std::exception`, no `what()`, no virtuals.
 */
class SipiValueError
{
public:
  /*!
   * \param[in] code The failure category.
   * \param[in] msg Error message describing the problem.
   * \param[in] errnum If a system call is the reason for the error, its
   *   `errno` value; `std::strerror(errnum)` is spliced into both message
   *   accessors when non-zero, mirroring `SipiImageError`'s behavior exactly
   *   so migrated call sites emit byte-identical text.
   * \param[in] loc The source location where the error originated.
   */
  explicit SipiValueError(ErrorCode code,
    std::string msg,
    int errnum = 0,
    const std::source_location &loc = std::source_location::current())
    : code_{ code }, errmsg_{ std::move(msg) }, errnum_{ errnum }, location_{ loc }
  {}

  [[nodiscard]] ErrorCode code() const { return code_; }

  /*!
   * Client-safe error message: path-redacted, WITHOUT the source location.
   * Mirrors `SipiImageError::message()`. Suitable for HTTP response bodies /
   * Lua-visible strings.
   */
  [[nodiscard]] std::string client_message() const
  {
    std::ostringstream err_stream;
    if (errnum_ != 0) { err_stream << "(system error: " << std::strerror(errnum_) << "): "; }
    err_stream << errmsg_;
    return redact_paths(err_stream.str());
  }

  /*!
   * Full diagnostic message: source file + line, NOT redacted. Mirrors
   * `SipiImageError::to_string()`. Formatted on demand, not cached.
   */
  [[nodiscard]] std::string diagnostic_message() const
  {
    std::ostringstream err_stream;
    err_stream << "Sipi image error at [" << location_.file_name() << ": " << location_.line() << "]";
    if (errnum_ != 0) { err_stream << " (system error: " << std::strerror(errnum_) << ")"; }
    err_stream << ": " << errmsg_;
    return err_stream.str();
  }

  /*!
   * Raw field accessors. They exist so a caller behind an exception-based
   * interface can reconstruct an equivalent `SipiImageError` — same message
   * text, same `errno`, same origin — from a `SipiValueError` produced by
   * `Result`-returning internals.
   */
  [[nodiscard]] const std::string &raw_message() const { return errmsg_; }

  [[nodiscard]] int errnum() const { return errnum_; }

  [[nodiscard]] const std::source_location &location() const { return location_; }

private:
  ErrorCode code_;
  std::string errmsg_;
  int errnum_;
  std::source_location location_;
};

template<typename T> using Result = std::expected<T, SipiValueError>;

}// namespace Sipi

#endif// SIPIVALUEERROR_HPP
