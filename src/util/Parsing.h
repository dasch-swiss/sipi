/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef __shttps_parsing_h
#define __shttps_parsing_h

#include <regex>
#include <string>
#include <unordered_map>
#include <vector>


/*!
 * \brief Parsing utilities.
 */
namespace shttps::Parsing {
// static std::unordered_map<std::string, std::string> mimetypes; //! format (key) to mimetype (value) conversion map

/*!
 * Parses a string containing a MIME type and optional character set, such as the Content-Type header defined by
 * <https://tools.ietf.org/html/rfc7231#section-3.1.1>.
 * @param mimestr a string containing the MIME type.
 * @return the MIME type and optional character set.
 */
std::pair<std::string, std::string> parseMimetype(const std::string &mimestr);


/*!
 * Determine the mimetype of a file using the magic number
 *
 * \param[in] fpath Path to file to check for the mimetype
 * \returns pair<string,string> containing the mimetype as first part
 *          and the charset as second part. Access as val.first and val.second!
 */
std::pair<std::string, std::string> getFileMimetype(const std::string &fpath);

/*!
 *
 * \param[in] fpath Path to file to check for the mimetype
 * \returns Best mimetype given magic number and extension
 */
std::string getBestFileMimetype(const std::string &fpath);

/*!
 *
 * @param path Path to file (
 * @param filename Original filename with extension
 * @param given_mimetype The mimetype given by the browser
 * @return true, if consistent, else false
 */
bool checkMimeTypeConsistency(const std::string &path,
  const std::string &filename,
  const std::string &given_mimetype = "");


/*!
 * Parses an integer.
 * @param str the string to be parsed.
 * @return the corresponding integer.
 */
size_t parse_int(std::string &str);

/*!
 * Parses a floating-point number containing only digits and an optional decimal point.
 *
 * @param str the string to be parsed.
 * @return the corresponding floating-point number.
 */
float parse_float(std::string &str);
}// namespace shttps::Parsing


#endif//__shttps_parsing_h
