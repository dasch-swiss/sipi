/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef __shttps_global_h
#define __shttps_global_h

#include <iostream>
#include <istream>
#include <sstream>
#include <string>
#include <utility>


namespace shttps {

template<typename Enumeration>
inline auto as_integer(Enumeration const value) -> typename std::underlying_type<Enumeration>::type
{
  return static_cast<typename std::underlying_type<Enumeration>::type>(value);
}
//-------------------------------------------------------------------------


inline std::string getFileName(const std::string &s)
{
  char sep = '/';
  size_t i = s.rfind(sep, s.length());

  if (i != std::string::npos) {
    return s.substr(i + 1, s.length() - i);
  } else {
    return s;
  }
}
}// namespace shttps

#endif
