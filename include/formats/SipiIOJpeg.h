/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * This file handles the reading and writing of JPEG 2000 files using libtiff.
 */
#ifndef _sipi_io_jpeg_h
#define _sipi_io_jpeg_h

#include <string>

#include "SipiIO.h"
#include "SipiImage.hpp"

namespace Sipi {

/*! Class which implements the JPEG2000-reader/writer */
class SipiIOJpeg : public SipiIO
{
private:
  static void parse_photoshop(SipiImage *img, char *data, int length);

public:
  ~SipiIOJpeg() override = default;
  ;

  /*!
   * Method used to read an image file
   *
   * \param *img Pointer to SipiImage instance
   * \param filepath Image file path
   * \param reduce Reducing factor. If it is not 0, the reader
   * only reads part of the data returning an image with reduces resolution.
   * If the value is 1, only half the resolution is returned. If it is 2, only one forth etc.
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
   * Write a JPEG image to a file, stdout or to a memory buffer
   *
   * \param *img Pointer to SipiImage instance
   * \param filepath Name of the image file to be written.
   */
  void write(SipiImage *img, const std::string &filepath, const SipiCompressionParams *params) override;
};

}// namespace Sipi


#endif
