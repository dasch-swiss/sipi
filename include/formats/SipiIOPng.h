/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * This file handles the reading and writing of PNG files using libpng.
 */
#ifndef __sipi_io_png_h
#define __sipi_io_png_h

#include <string>

#include "../../src/SipiImage.hpp"
#include "SipiIO.h"

namespace Sipi {

class SipiIOPng : public SipiIO
{
public:
  ~SipiIOPng() override = default;
  ;

  /*!
   * Method used to read an image file
   *
   * \param *img Pointer to SipiImage instance
   * \param filepath Image file path
   * \param reduce Reducing factor. Not used reading TIFF files
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
   * \param[in] filepath Pathname of the image file
   * \param[out] width Width of the image in pixels
   * \param[out] height Height of the image in pixels
   */
  Sipi::SipiImgInfo getDim(const std::string &filepath) override;


  /*!
   * Write a PNG image to a file, stdout or to a memory buffer
   *
   * If the filepath is "-", the PNG file is built in an internal memory buffer
   * and after finished transfered to stdout. This is necessary because libtiff
   * makes extensive use of "lseek" which is not available on stdout!
   *
   * \param *img Pointer to SipiImage instance
   * \param filepath Name of the image file to be written. Please note that
   * - "-" means to write the image data to stdout
   * - "HTTP" means to write the image data to the HTTP-server output
   */
  void write(SipiImage *img, const std::string &filepath, const SipiCompressionParams *params) override;
};
}// namespace Sipi


#endif
