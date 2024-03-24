/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <sstream>


#include "SipiError.hpp"

namespace Sipi {

SipiError::SipiError(const char *msg, int errno_p, const std::source_location &loc) : Error{ msg, errno_p, loc } {}

//============================================================================


SipiError::SipiError(const std::string &msg, int errno_p, const std::source_location &loc) : Error{ msg, errno_p, loc }
{}

//============================================================================

std::ostream &operator<<(std::ostream &outStream, const SipiError &rhs)
{
  const std::string errStr = rhs.to_string();
  outStream << errStr << '\n';// TODO: remove the endl, the logging code should do it
  return outStream;
}

//============================================================================


}// namespace Sipi
