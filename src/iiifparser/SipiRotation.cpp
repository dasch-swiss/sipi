/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <assert.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <vector>

#include <stdio.h>
#include <string.h>

#include "../SipiError.hpp"
#include "SipiRotation.h"
#include "shttps/Parsing.h"

static const char __file__[] = __FILE__;

namespace Sipi {

SipiRotation::SipiRotation()
{
  mirror = false;
  rotation = 0.F;
  return;
}

SipiRotation::SipiRotation(std::string str)
{
  try {
    if (str.empty()) {
      mirror = false;
      rotation = 0.F;
      return;
    }

    mirror = str[0] == '!';
    if (mirror) { str.erase(0, 1); }

    rotation = shttps::Parsing::parse_float(str);
  } catch (shttps::Error &error) {
    throw SipiError(__file__, __LINE__, "Could not parse IIIF rotation parameter: " + str);
  }
}
//-------------------------------------------------------------------------


//-------------------------------------------------------------------------
// Output to stdout for debugging etc.
//
std::ostream &operator<<(std::ostream &outstr, const SipiRotation &rhs)
{
  outstr << "IIIF-Server Rotation parameter:";
  outstr << "  Mirror " << rhs.mirror;
  outstr << " | rotation = " << std::to_string(rhs.rotation);
  return outstr;
}
//-------------------------------------------------------------------------
}// namespace Sipi
