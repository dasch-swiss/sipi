/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <cstring>// std::strerror
#include <sstream>// std::ostringstream

#include "Error.h"

namespace shttps {

Error::Error(const char *file_p, const int line_p, const char *msg, int errno_p)
  : runtime_error(
      std::string(msg) + "\nFile: " + std::string(file_p) + std::string(" Line: ") + std::to_string(line_p)),
    line(line_p), file(file_p), message(msg), sysErrno(errno_p)
{}
//============================================================================


Error::Error(const char *file_p, const int line_p, const std::string &msg, int errno_p)
  : runtime_error(
      std::string(msg) + "\nFile: " + std::string(file_p) + std::string(" Line: ") + std::to_string(line_p)),
    line(line_p), file(file_p), message(msg), sysErrno(errno_p)
{}
//============================================================================

std::string Error::to_string(void) const
{
  std::ostringstream err_stream;
  err_stream << "Error at [" << file << ": " << line << "]";
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
