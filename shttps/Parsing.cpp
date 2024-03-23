/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <regex>
#include <sstream>

#include "Error.h"
#include "Parsing.h"

#include "magic.h"

static const char __file__[] = __FILE__;

namespace shttps::Parsing {

static std::unordered_map<std::string, std::vector<std::string>> mimetypes = { { "jpg", { "image/jpeg" } },
  { "jpeg", { "image/jpeg" } },
  { "jpx", { "image/jpx", "image/jp2" } },
  { "jp2", { "image/jp2", "image/jpx" } },
  { "tiff", { "image/tiff" } },
  { "tif", { "image/tiff" } },
  { "png", { "image/png" } },
  { "webp", { "image/webp" } },
  { "bmp", { "image/x-ms-bmp" } },
  { "gif", { "image/gif" } },
  { "ico", { "image/x-icon" } },
  { "pnm", { "image/x-portable-anymap" } },
  { "pbm", { "image/x-portable-bitmap" } },
  { "pgm", { "image/x-portable-graymap" } },
  { "ppm", { "image/x-portable-pixmap" } },
  { "svg", { "image/svg+xml" } },
  { "pdf", { "application/pdf" } },
  { "dwg", { "application/acad" } },
  { "gz", { "application/gzip" } },
  { "json", { "application/json" } },
  { "js", { "application/javascript" } },
  { "xls", { "application/vnd.ms-excel", "application/msexcel" } },
  { "xla", { "application/vnd.ms-excel", "application/msexcel" } },
  { "xlsx", { "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "application/vnd.ms-excel" } },
  { "ppt", { "application/vnd.ms-powerpoint", "application/mspowerpoint" } },
  { "pptx", { "application/vnd.ms-powerpoint", "application/mspowerpoint" } },
  { "pps", { "application/vnd.ms-powerpoint", "application/mspowerpoint" } },
  { "pot", { "application/vnd.ms-powerpoint", "application/mspowerpoint" } },
  { "doc", { "application/msword" } },
  { "docx", { "application/vnd.openxmlformats-officedocument.wordprocessingml.document" } },
  { "bin", { "application/octet-stream" } },
  { "dat", { "application/octet-stream" } },
  { "ps", { "application/postscript" } },
  { "eps", { "application/postscript" } },
  { "ai", { "application/postscript" } },
  { "rtf", { "application/rtf", "text/rtf" } },
  { "htm", { "text/html", "application/xhtml+xml" } },
  { "html", { "text/html", "application/xhtml+xml" } },
  { "shtml", { "text/html", "application/xhtml+xml" } },
  { "xhtml", { "application/xhtml+xml" } },
  { "css", { "text/css" } },
  { "xml", { "text/xml", "application/xml" } },
  { "odd", { "text/xml", "text/html" } },
  { "rng", { "text/xml", "application/xml", "text/html" } },
  { "z", { "application/x-compress" } },
  { "tgz", { "application/x-compress" } },
  { "dvi", { "application/x-dvi" } },
  { "gtar", { "application/x-gtar" } },
  { "hdf", { "application/x-hdf" } },
  { "php", { "application/x-httpd-php" } },
  { "phtml", { "application/x-httpd-php" } },
  { "tex", { "application/x-tex", "application/x-latex" } },
  { "latex", { "application/x-latex" } },
  { "texi", { "application/x-texinfo" } },
  { "texinfo", { "application/x-texinfo" } },
  { "tar", { "application/x-tar" } },
  { "zip", { "application/zip" } },
  { "au", { "audio/basic" } },
  { "snd", { "audio/basic" } },
  { "mp3", { "audio/mpeg" } },
  { "mp4", { "audio/mp4" } },
  { "ogg", { "application/ogg", "audio/ogg", "video/ogg" } },
  { "ogv", { "video/ogg", "application/ogg" } },
  { "oga", { "audio/ogg", "application/ogg" } },
  { "wav", { "audio/wav", "audio/wav", "audio/x-wav", "audio/x-pn-wav" } },
  { "aif", { "audio/x-aiff" } },
  { "aiff", { "audio/x-aiff" } },
  { "aifc", { "audio/x-aiff" } },
  { "mid", { "audio/x-midi" } },
  { "midi", { "audio/x-midi" } },
  { "mp2", { "audio/x-mpeg" } },
  { "wrl", { "model/vrml", "x-world/x-vrml" } },
  { "ics", { "text/calendar" } },
  { "csv", { "text/csv", "application/csv", "text/plain", "application/vnd.ms-excel", "text/comma-separated-values" } },
  { "tsv", { "text/tab-separated-values", "text/plain", "text/x-csv" } },
  { "txt", { "text/plain" } },
  { "rtx", { "text/richtext" } },
  { "sgm", { "text/x-sgml" } },
  { "sgml", { "text/x-sgml" } },
  { "mpeg", { "video/mpeg" } },
  { "mpg", { "video/mpeg" } },
  { "mpe", { "video/mpeg" } },
  { "mp4", { "video/mp4" } },
  { "mov", { "video/quicktime" } },
  { "qt", { "video/quicktime" } },
  { "viv", { "video/vnd.vivo" } },
  { "vivo", { "video/vnd.vivo" } },
  { "webm", { "video/webm" } },
  { "avi", { "video/x-msvideo" } },
  { "3gp", { "video/3gpp" } },
  { "movie", { "video/x-sgi-movie" } },
  { "swf", { "application/x-shockwave-flash" } },
  { "vcf", { "text/x-vcard" } } };

std::pair<std::string, std::string> parseMimetype(const std::string &mimestr)
{
  try {
    // A regex for parsing the value of an HTTP Content-Type header. In C++11, initialization of this
    // static local variable happens once and is thread-safe.
    static std::regex mime_regex(
      "^([^;]+)(;\\s*charset=\"?([^\"]+)\"?)?$", std::regex_constants::ECMAScript | std::regex_constants::icase);

    std::smatch mime_match;
    std::string mimetype;
    std::string charset;

    if (std::regex_match(mimestr, mime_match, mime_regex)) {
      if (mime_match.size() > 1) {
        mimetype = mime_match[1].str();

        if (mime_match.size() == 4) { charset = mime_match[3].str(); }
      }
    } else {
      std::ostringstream error_msg;
      error_msg << "Could not parse MIME type: " << mimestr;
      throw Error(__file__, __LINE__, error_msg.str());
    }

    // Convert MIME type and charset to lower case
    std::transform(mimetype.begin(), mimetype.end(), mimetype.begin(), ::tolower);
    std::transform(charset.begin(), charset.end(), charset.begin(), ::tolower);

    return std::make_pair(mimetype, charset);
  } catch (std::regex_error &e) {
    std::ostringstream error_msg;
    error_msg << "Regex error: " << e.what();
    throw Error(__file__, __LINE__, error_msg.str());
  }
}
//=============================================================================================================

std::pair<std::string, std::string> getFileMimetype(const std::string &fpath)
{
  magic_t handle;
  if ((handle = magic_open(MAGIC_MIME | MAGIC_PRESERVE_ATIME)) == nullptr) {
    throw Error(__file__, __LINE__, magic_error(handle));
  }

  if (magic_load(handle, nullptr) != 0) { throw Error(__file__, __LINE__, magic_error(handle)); }


  std::string mimestr(magic_file(handle, fpath.c_str()));
  magic_close(handle);
  return parseMimetype(mimestr);
}
//=============================================================================================================

std::string getBestFileMimetype(const std::string &fpath)
{
  std::string mimetype = getFileMimetype(fpath).first;
  size_t dot_pos = fpath.find_last_of(".");
  if (dot_pos != std::string::npos) {
    std::string extension = fpath.substr(dot_pos + 1);
    std::vector<std::string> mimes_from_extension;
    try {
      mimes_from_extension = mimetypes.at(extension);
    } catch (std::out_of_range const &exc) {
      return mimetype;
    }
    //
    // check if mimetype from magic number is in list if this extension. If so, return the preferred
    // mimetype (= first in array). If not, return the mimtype from the magic number (extension could be
    // wrong!)
    //
    bool found = false;
    for (const auto &mt : mimes_from_extension) {
      if (mt == mimetype) {
        found = true;
        break;
      }
    }
    if (found) {
      return mimes_from_extension[0];
    } else {
      return mimetype;
    }
  }
  return mimetype;
}
//=============================================================================================================

bool checkMimeTypeConsistency(const std::string &path) { return checkMimeTypeConsistency(path, path); }
//=============================================================================================================

bool checkMimeTypeConsistency(const std::string &path, const std::string &filename, const std::string &given_mimetype)
{
  try {
    //
    // first we get the possible mimetypes associated with the given filename extension
    //
    size_t dot_pos = filename.find_last_of(".");
    if (dot_pos == std::string::npos) {
      std::stringstream ss;
      ss << "Invalid filename without extension: \"" << filename << "\"";
      throw Error(__file__, __LINE__, ss.str());
    }
    std::string extension = filename.substr(dot_pos + 1);
    // convert file extension to lower case (uppercase letters in file extension have to be converted for
    // mime type comparison)
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    std::vector<std::string> mime_from_extension = mimetypes.at(extension);

    //
    // now we get the mimetype determined by the magic number
    //
    std::pair<std::string, std::string> actual_mimetype = getFileMimetype(path);

    //
    // now we test if the mimetype given given by the magic number corresponds to a valid mimetype for this extension
    //
    bool got1 = false;
    for (const auto &mt : mime_from_extension) {
      if (mt == actual_mimetype.first) {
        got1 = true;
        break;
      }
    }
    if (!got1) return false;

    if (!given_mimetype.empty()) {
      //
      // now we test if the expected mimetype corresponds to a valid mimetype for this extension
      //
      bool got2 = false;
      for (const auto &mt : mime_from_extension) {
        if (mt == given_mimetype) {
          got2 = true;
          break;
        }
      }
      if (!got2) return false;
    }
  } catch (std::out_of_range &e) {
    std::stringstream ss;
    ss << "Unsupported file type: \"" << filename << "\"";
    throw shttps::Error(__file__, __LINE__, ss.str());
  }

  return true;
}
//=============================================================================================================

size_t parse_int(std::string &str)
{
  try {
    // A regex for parsing an integer containing only digits. In C++11, initialization of this
    // static local variable happens once and is thread-safe.
    static std::regex int_regex("^[0-9]+$", std::regex_constants::ECMAScript);
    std::smatch int_match;

    if (std::regex_match(str, int_match, int_regex)) {
      std::stringstream sstream(int_match[0]);
      size_t result;
      sstream >> result;
      return result;
    } else {
      std::ostringstream error_msg;
      error_msg << "Could not parse integer: " << str;
      throw Error(__file__, __LINE__, error_msg.str());
    }
  } catch (std::regex_error &e) {
    std::ostringstream error_msg;
    error_msg << "Regex error: " << e.what();
    throw Error(__file__, __LINE__, error_msg.str());
  }
}
//=============================================================================================================

float parse_float(std::string &str)
{
  try {
    // A regex for parsing a floating-point number containing only digits and an optional decimal point. In C++11,
    // initialization of this static local variable happens once and is thread-safe.
    static std::regex float_regex("^[0-9]+(\\.[0-9]+)?$", std::regex_constants::ECMAScript);
    std::smatch float_match;

    if (std::regex_match(str, float_match, float_regex)) {
      std::stringstream sstream(float_match[0]);
      float result;
      sstream >> result;
      return result;
    } else {
      std::ostringstream error_msg;
      error_msg << "Could not parse floating-point number: " << str;
      throw Error(__file__, __LINE__, error_msg.str());
    }
  } catch (std::regex_error &e) {
    std::ostringstream error_msg;
    error_msg << "Regex error: " << e.what();
    throw Error(__file__, __LINE__, error_msg.str());
  }
}
//=============================================================================================================

}// namespace shttps::Parsing
