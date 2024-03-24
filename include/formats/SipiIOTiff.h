/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * This file handles the reading and writing of TIFF files using libtiff.
 */
#ifndef __sipi_io_tiff_h
#define __sipi_io_tiff_h

#include <string>

#include "tiff.h"
#include "tiffio.h"

#include "../../src/SipiImage.hpp"
#include "SipiIO.h"

namespace Sipi {

unsigned char *read_watermark(const std::string &wmfile, int &nx, int &ny, int &nc);

/*! Class which implements the TIFF-reader/writer */
class SipiIOTiff : public SipiIO
{
private:
  /*!
   * Read the EXIF data from the TIFF file and create an Exiv2::Exif object
   * \param img Pointer to SipiImage instance
   * \param[in] tif Pointer to TIFF file handle
   * \param[in] exif_offset Offset of EXIF directory in TIFF file
   */
  void readExif(SipiImage *img, TIFF *tif, toff_t exif_offset);

  /*!
   * Write the EXIF data to the TIFF file
   * \param img Pointer to SipiImage instance
   * \param[in] tif Pointer to TIFF file handle
   */
  void writeExif(SipiImage *img, TIFF *tif);

  /*!
   * Converts an image from RRRRRR...GGGGGG...BBBBB to RGBRGBRGBRGB....
   * \param img Pointer to SipiImage instance
   * \param[in] sll Scanline length in bytes
   */
  void separateToContig(SipiImage *img, unsigned int sll);

  /*!
   * Converts a bitonal 1 bit image to a bitonal 8 bit image
   *
   * \param img Pointer to SipiImage instance
   * \param[in] Length of scanline in bytes
   * \param[in] Value to be used for black pixels
   * \param[in] Value to be used for white pixels
   */
  void cvrt1BitTo8Bit(SipiImage *img, unsigned int sll, unsigned int black, unsigned int white);

  /*!
   * Converts a 8 bps bitonal image to 1 bps bitonal image
   *
   * \param[in] img Reference to SipiImage instance
   * \param[out] sll Scan line lengt
   * \returns Buffer of 1-bit data (padded to bytes). NOTE: This buffer has to be deleted by the caller!
   */
  unsigned char *cvrt8BitTo1bit(const SipiImage &img, unsigned int &sll);

public:
  virtual ~SipiIOTiff(){};

  static void initLibrary(void);

  /*!
   * Method used to read an image file
   *
   * \param img Pointer to SipiImage instance
   * \param filepath Image file path
   * \param region Region of the image to read
   * \param size Size of the image to read
   * \param force_bps_8 Convert the file to 8 bits/sample on reading thus enforcing an 8 bit image
   * \param scaling_quality Quality of the scaling algorithm
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
   * \return Image information
   */
  SipiImgInfo getDim(const std::string &filepath) override;

  /*!
   * Write a TIFF image to a file, stdout or to a memory buffer
   *
   * If the filepath is "-", the TIFF file is built in an internal memory buffer
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
