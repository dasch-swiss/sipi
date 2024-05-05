/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * This file handles the reading and writing of JPEG 2000 files using libtiff.
 */
#ifndef __sipi_io_j2k_h
#define __sipi_io_j2k_h

#include <string>

#include "tiff.h"
#include "tiffio.h"

#include "SipiImage.hpp"
// #include "metadata/SipiExif.h"
#include "SipiIO.h"

namespace Sipi {

/*! Class which implements the JPEG2000-reader/writer */
class SipiIOJ2k : public SipiIO
{
private:
public:
  ~SipiIOJ2k() override = default;
  ;
  /*!
   * Method used to read an image file
   *
   * \param img Pointer to SipiImage instance
   * \param filepath Image file path
   * \param region Region of the image to be read
   * \param size Size of the image to be read
   * \param force_bps_8 Force the image to be read as 8 bits per sample
   * \param scaling_quality Scaling quality for the different formats
   */
  bool read(SipiImage *img,
    const std::string &filepath,
    std::shared_ptr<SipiRegion> region,
    std::shared_ptr<SipiSize> size,
    bool force_bps_8,
    ScalingQuality scaling_quality) override;

  /*!
   * Get the dimension of the image
   *
   * \param filepath Pathname of the image file
   */
  Sipi::SipiImgInfo getDim(const std::string &filepath) override;

  /*!
   * Write a TIFF image to a file, stdout or to a memory buffer
   *
   * \param *img Pointer to SipiImage instance
   * \param filepath Name of the image file to be written.
   */
  void write(SipiImage *img, const std::string &filepath, const SipiCompressionParams *params) override;
};
}// namespace Sipi

#endif
