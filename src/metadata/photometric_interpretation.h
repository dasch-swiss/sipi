/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * The TIFF photometric-interpretation enum, factored out of `SipiImage.h` so
 * the metadata layer (e.g. `Icc`) can reason about an image's colour space
 * without depending on `SipiImage` itself — `SipiImage` (the image engine,
 * above metadata) includes this header for its `photo` member, and `Icc`
 * (metadata) consumes it to build an lcms2 pixel format.
 */
#ifndef SIPI_METADATA_PHOTOMETRIC_INTERPRETATION_H
#define SIPI_METADATA_PHOTOMETRIC_INTERPRETATION_H

#include <cstdint>
#include <string>

namespace Sipi {

/*! Implements the values of the photometric tag of the TIFF format */
enum class PhotometricInterpretation : std::uint16_t {
  MINISWHITE = 0,//!< B/W or gray value image with 0 = white and 1 (255) = black
  MINISBLACK = 1,//!< B/W or gray value image with 0 = black and 1 (255) = white (is default in SIPI)
  RGB = 2,//!< Color image with RGB values
  PALETTE = 3,//!< Palette color image, is not suppoted by Sipi
  MASK = 4,//!< Mask image, not supported by Sipi
  SEPARATED = 5,//!< Color separated image, is assumed to be CMYK
  YCBCR = 6,//!< Color representation with YCbCr, is supported by Sipi, but converted to an ordinary RGB
  CIELAB = 8,//!< CIE*a*b image, only very limited support (untested!)
  ICCLAB = 9,//!< ICCL*a*b image, only very limited support (untested!)
  ITULAB = 10,//!< ITUL*a*b image, not supported yet (what is this by the way?)
  CFA = 32803,//!< Color field array, used for DNG and RAW image. Not supported!
  LOGL = 32844,//!< LOGL format (not supported)
  LOGLUV = 32845,//!< LOGLuv format (not supported)
  LINEARRAW = 34892,//!< Linear raw array for DNG and RAW formats. Not supported!
  INVALID = 65535//!< an invalid value
};

inline auto to_string(const PhotometricInterpretation photo) -> std::string
{
  switch (photo) {
  case PhotometricInterpretation::MINISWHITE:
    return "MINISWHITE";
  case PhotometricInterpretation::MINISBLACK:
    return "MINISBLACK";
  case PhotometricInterpretation::RGB:
    return "RGB";
  case PhotometricInterpretation::PALETTE:
    return "PALETTE";
  case PhotometricInterpretation::MASK:
    return "MASK";
  case PhotometricInterpretation::SEPARATED:
    return "SEPARATED";
  case PhotometricInterpretation::YCBCR:
    return "YCBCR";
  case PhotometricInterpretation::CIELAB:
    return "CIELAB";
  case PhotometricInterpretation::ICCLAB:
    return "ICCLAB";
  case PhotometricInterpretation::ITULAB:
    return "ITULAB";
  case PhotometricInterpretation::CFA:
    return "CFA";
  case PhotometricInterpretation::LOGL:
    return "LOGL";
  case PhotometricInterpretation::LOGLUV:
    return "LOGLUV";
  case PhotometricInterpretation::LINEARRAW:
    return "LINEARRAW";
  case PhotometricInterpretation::INVALID:
    return "INVALID";
  default:
    return "UNKNOWN";
  }
}

}// namespace Sipi

#endif// SIPI_METADATA_PHOTOMETRIC_INTERPRETATION_H
