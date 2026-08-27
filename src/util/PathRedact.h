/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * Redact filesystem-path directory prefixes from client-facing error text.
 *
 * `redact_paths` collapses every whitespace-delimited token containing a
 * `'/'` down to the substring after its last `'/'`, so a throw-site message
 * like `Cannot read file "/srv/images/sub/foo.jp2": broken` becomes
 * `Cannot read file "foo.jp2": broken`. This keeps error text useful for a
 * caller while dropping the server's imgroot/docroot layout. Used only by
 * client-facing accessors (`SipiImageError::message()`,
 * `shttps::Error::message()`); `to_string()`/`what()` keep full paths for
 * server-side logs.
 */
#ifndef SIPI_UTIL_PATH_REDACT_H
#define SIPI_UTIL_PATH_REDACT_H

#include <cctype>
#include <string>

namespace Sipi {

/*!
 * Collapse directory prefixes out of whitespace-delimited tokens containing
 * a `'/'`, replacing each such token with the substring after its last
 * `'/'`. Tokens without a `'/'` and the original whitespace separators are
 * left unchanged.
 *
 * \param s The message text to redact.
 * \returns The redacted message text.
 */
[[nodiscard]] inline std::string redact_paths(const std::string &s)
{
  std::string result;
  result.reserve(s.size());

  std::size_t token_start = 0;
  while (token_start < s.size()) {
    std::size_t token_end = token_start;
    while (token_end < s.size() && !std::isspace(static_cast<unsigned char>(s[token_end]))) { ++token_end; }

    const std::string token = s.substr(token_start, token_end - token_start);
    const std::size_t last_slash = token.find_last_of('/');
    if (last_slash != std::string::npos) {
      result += token.substr(last_slash + 1);
    } else {
      result += token;
    }

    token_start = token_end;
    while (token_start < s.size() && std::isspace(static_cast<unsigned char>(s[token_start]))) {
      result += s[token_start];
      ++token_start;
    }
  }

  return result;
}

}// namespace Sipi

#endif// SIPI_UTIL_PATH_REDACT_H
