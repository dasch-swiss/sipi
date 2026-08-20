/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "SipiConf.h"
#include "logging/logger.h"
#include <stdexcept>
#include <string>
#include <thread>

namespace Sipi {

long long parseSizeString(const std::string &str)
{
  if (str.empty()) return 0;
  if (str == "-1") return -1;

  size_t l = str.length();
  char c = str[l - 1];
  if (c == 'M' || c == 'm') {
    return std::stoll(str.substr(0, l - 1)) * 1024 * 1024;
  } else if (c == 'G' || c == 'g') {
    return std::stoll(str.substr(0, l - 1)) * 1024 * 1024 * 1024;
  } else {
    return std::stoll(str);
  }
}

SipiConf::SipiConf()
{
  // A default-constructed config backs the Lua-less (TOML) init path. Mirror the
  // Lua ctor's scaling-quality defaults here so an omitted [image].scaling_quality
  // behaves identically on the Lua and TOML paths (the scalar defaults come from
  // the in-class member initializers in SipiConf.h). Without this, an empty map
  // makes to_scaling_quality() fall every codec to HIGH — diverging from Lua's
  // jpeg=MEDIUM default.
  scaling_quality = { { "jpeg", "medium" }, { "tiff", "high" }, { "png", "high" }, { "j2k", "high" } };
}

}// namespace Sipi
