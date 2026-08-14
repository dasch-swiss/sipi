/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SHTTPS_UTIL_URLDECODE_H
#define SHTTPS_UTIL_URLDECODE_H

#include <string>

namespace shttps {

/*!
 * Percent-decode a URL-encoded string. A generic string utility (not HTTP
 * transport): the IIIF identifier parser and the request handlers decode URL
 * components with it, so it lives in the leaf util package rather than in
 * `transport/Connection`.
 *
 * \param src The URL-encoded string.
 * \param form_encoded When true, also decode `+` to space (application/
 *        x-www-form-urlencoded). Malformed/trailing `%`-escapes pass through
 *        literally.
 * \returns The decoded string.
 */
std::string urldecode(const std::string &src, bool form_encoded = false);

}// namespace shttps

#endif// SHTTPS_UTIL_URLDECODE_H
