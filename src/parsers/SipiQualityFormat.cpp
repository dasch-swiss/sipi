/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */


#include <fstream>

#include "parsers/SipiQualityFormat.h"
#include "SipiError.hpp"


#include <string>

namespace Sipi {

SipiQualityFormat::SipiQualityFormat(std::string str)
{
  if (str.empty()) {
    quality_type = SipiQualityFormat::DEFAULT;
    format_type = SipiQualityFormat::JPG;
    return;
  }

  size_t dot_pos = str.find(".");

  if (dot_pos == std::string::npos) {
    throw SipiError("IIIF Error reading Quality+Format parameter  \"" + str + "\" !");
  }

  std::string quality = str.substr(0, dot_pos);
  std::string format = str.substr(dot_pos + 1);

  if (quality == "default") {
    quality_type = SipiQualityFormat::DEFAULT;
  } else if (quality == "color") {
    quality_type = SipiQualityFormat::COLOR;
  } else if (quality == "gray") {
    quality_type = SipiQualityFormat::GRAY;
  } else if (quality == "bitonal") {
    quality_type = SipiQualityFormat::BITONAL;
  } else {
    throw SipiError("IIIF Error reading Quality parameter  \"" + quality + "\" !");
  }

  if (format == "jpg") {
    format_type = SipiQualityFormat::JPG;
  } else if (format == "tif") {
    format_type = SipiQualityFormat::TIF;
  } else if (format == "png") {
    format_type = SipiQualityFormat::PNG;
  } else if (format == "gif") {
    format_type = SipiQualityFormat::GIF;
  } else if (format == "jp2") {
    format_type = SipiQualityFormat::JP2;
  } else if (format == "pdf") {
    format_type = SipiQualityFormat::PDF;
  } else if (format == "webp") {
    format_type = SipiQualityFormat::WEBP;
  } else {
    format_type = SipiQualityFormat::UNSUPPORTED;
  }
}


//-------------------------------------------------------------------------
// Output to stdout for debugging etc.
//
std::ostream &operator<<(std::ostream &outstr, const SipiQualityFormat &rhs)
{
  outstr << "IIIF-Server QualityFormat parameter: ";
  outstr << "  Quality: " << rhs.quality_type;
  outstr << " | Format: " << rhs.format_type;
  return outstr;
}
//-------------------------------------------------------------------------

}
