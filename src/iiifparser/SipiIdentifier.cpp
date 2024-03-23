/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <cstdlib>
#include <cstring>

#include "../SipiError.hpp"
#include "SipiIdentifier.h"
#include "shttps/Connection.h"

static const char __file__[] = __FILE__;

namespace Sipi {
SipiIdentifier::SipiIdentifier(const std::string &str)
{
  size_t pos;
  if ((pos = str.find("@")) != std::string::npos) {
    identifier = shttps::urldecode(str.substr(0, pos));
    try {
      page = stoi(str.substr(pos + 1));
    } catch (const std::invalid_argument &ia) {
      page = 0;
    } catch (const std::out_of_range &oor) {
      page = 0;
    }
  } else {
    identifier = shttps::urldecode(str);
    page = 0;
  }
}
}// namespace Sipi
