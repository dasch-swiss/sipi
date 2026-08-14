/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "UrlDecode.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace shttps {

std::string urldecode(const std::string &src, bool form_encoded)
{

#define HEXTOI(x) (isdigit(x) ? x - '0' : x - 'W')

  std::stringstream outss;
  size_t start = 0;
  size_t pos;

  while ((pos = src.find('%', start)) != std::string::npos) {
    const bool valid_escape = (pos + 2) < src.length() && isxdigit(src[pos + 1]) && isxdigit(src[pos + 2]);

    if (valid_escape) {
      std::string tmpstr = src.substr(start, pos - start);
      if (form_encoded) {
        for (size_t i = 0; i < tmpstr.length(); i++) {
          if (tmpstr[i] == '+') { tmpstr[i] = ' '; }
        }
      }
      outss << tmpstr;

      // we have a valid hex number
      char a = (char)tolower(src[pos + 1]);
      char b = (char)tolower(src[pos + 2]);
      char c = ((HEXTOI(a) << 4) | HEXTOI(b));
      outss << c;
      start = pos + 3;
    } else {
      // Malformed (or trailing) %-escape: pass the bytes through literally.
      // Advance by min(3, remaining) so a % in the last two positions still
      // moves past itself.
      const size_t end = pos + std::min<size_t>(3, src.length() - pos);
      std::string tmpstr = src.substr(start, end - start);
      if (form_encoded) {
        for (size_t i = 0; i < tmpstr.length(); i++) {
          if (tmpstr[i] == '+') { tmpstr[i] = ' '; }
        }
      }
      outss << tmpstr;
      start = end;
    }
  }

  outss << src.substr(start, src.length() - start);
  return outss.str();
}

}// namespace shttps
