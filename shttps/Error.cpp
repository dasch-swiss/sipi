/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <cstring>// std::strerror
#include <sstream>// std::ostringstream

#include "Error.h"

#include <source_location>

namespace shttps {

Error::Error(const char *msg, int errno_p, const std::source_location &loc)
  : runtime_error(std::string(msg) + "\nFile: " + std::string(loc.file_name()) + std::string(" Line: ")
                  + std::to_string(loc.line())),
    message{ msg }, sysErrno{ errno_p }, location{ loc }
{}
//============================================================================


Error::Error(const std::string &msg, int errno_p, const std::source_location &loc)
  : runtime_error(std::string(msg) + "\nFile: " + std::string(loc.file_name()) + std::string(" Line: ")
                  + std::to_string(loc.line())),
    message{ msg }, sysErrno{ errno_p }, location{ loc }
{}
//============================================================================

std::string Error::to_string() const
{
  std::ostringstream err_stream;
  err_stream << "Error at [" << location.file_name() << ": " << location.line() << "]";
  if (sysErrno != 0) err_stream << " (system error: " << std::strerror(sysErrno) << ")";
  err_stream << ": " << message;
  return err_stream.str();
}
//============================================================================

std::ostream &operator<<(std::ostream &out_stream, const Error &rhs)
{
  std::string errStr = rhs.to_string();
  out_stream << errStr;
  return out_stream;
}
//============================================================================

}// namespace shttps
