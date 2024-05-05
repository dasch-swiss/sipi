/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_SIPIQUALITYFORMAT_H
#define SIPI_SIPIQUALITYFORMAT_H

#include <string>


namespace Sipi {
class SipiQualityFormat
{
public:
  typedef enum { DEFAULT, COLOR, GRAY, BITONAL } QualityType;
  typedef enum { UNSUPPORTED, JPG, TIF, PNG, GIF, JP2, PDF, WEBP } FormatType;

private:
  QualityType quality_type;
  FormatType format_type;

public:
  inline SipiQualityFormat()
  {
    quality_type = SipiQualityFormat::DEFAULT;
    format_type = SipiQualityFormat::JPG;
  }

  SipiQualityFormat(std::string str);

  friend std::ostream &operator<<(std::ostream &lhs, const SipiQualityFormat &rhs);

  inline QualityType quality() { return quality_type; };

  inline FormatType format() { return format_type; };
};

}// namespace Sipi

#endif// SIPI_SIPIQUALITYFORMAT_H
