/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <cstdlib>
#include <cstring>
#include <sstream>


#include "SipiError.hpp"

namespace Sipi {

SipiError::SipiError(const char *file_p, const int line_p, const char *msg, int errno_p)
  : Error(file_p, line_p, msg, errno_p)
{}

//============================================================================


SipiError::SipiError(const char *file_p, const int line_p, const std::string &msg, int errno_p)
  : Error(file_p, line_p, msg, errno_p)
{}

//============================================================================

std::string SipiError::to_string() const
{
  std::ostringstream errStream;
  errStream << "Sipi Error at [" << file << ": " << line << "]";
  if (sysErrno != 0) errStream << " (system error: " << std::strerror(sysErrno) << ")";
  errStream << ": " << message;
  return errStream.str();
}

//============================================================================

std::ostream &operator<<(std::ostream &outStream, const SipiError &rhs)
{
  const std::string errStr = rhs.to_string();
  outStream << errStr << std::endl;// TODO: remove the endl, the logging code should do it
  return outStream;
}

//============================================================================


}// namespace Sipi
