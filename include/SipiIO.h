/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * This file implements the virtual abstract class which implements the image file I/O.
 */
#ifndef _sipi_io_h
#define _sipi_io_h

#include <cstdint>
#include <string>
#include <unordered_map>

#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiSize.h"

#include <memory>

/**
 * @namespace Sipi Is used for all Sipi things.
 */
namespace Sipi {

enum class ScalingMethod : std::uint8_t { HIGH = 0, MEDIUM = 1, LOW = 2 };

struct ScalingQuality
{
  ScalingMethod jk2;
  ScalingMethod jpeg;
  ScalingMethod tiff;
  ScalingMethod png;
};

enum Orientation : std::uint8_t {
  // from the TIFF specification...
  TOPLEFT = 1,
  //!< The 0th row represents the visual top of the image, and the 0th column represents the visual left-hand side.
  TOPRIGHT = 2,
  //!< The 0th row represents the visual top of the image, and the 0th column represents the visual right-hand side.
  BOTRIGHT = 3,
  //!< The 0th row represents the visual bottom of the image, and the 0th column represents the visual right-hand side.
  BOTLEFT = 4,
  //!< The 0th row represents the visual bottom of the image, and the 0th column represents the visual left-hand side.
  LEFTTOP = 5,
  //!< The 0th row represents the visual left-hand side of the image, and the 0th column represents the visual top.
  RIGHTTOP = 6,
  //!< The 0th row represents the visual right-hand side of the image, and the 0th column represents the visual top.
  RIGHTBOT = 7,
  //!< The 0th row represents the visual right-hand side of the image, and the 0th column represents the visual bottom.
  LEFTBOT = 8
  //!< The 0th row represents the visual left-hand side of the image, and the 0th column represents the visual bottom.
};

typedef struct SubImageInfo_
{
  uint32_t reduce; // equal to image.nx / SubImageInfo.width
  uint32_t width;
  uint32_t height;
  uint32_t tile_width;
  uint32_t tile_height;
} SubImageInfo;

class SipiImgInfo
{
public:
  enum : std::uint8_t { FAILURE = 0, DIMS = 1, ALL = 2 } success{ FAILURE };

  int width{ 0 };
  int height{ 0 };
  Orientation orientation{ TOPLEFT };
  int tile_width{ 0 };
  int tile_height{ 0 };
  int clevels{ 0 };
  int numpages{ 0 };
  std::string internalmimetype;
  std::string origname;
  std::string origmimetype;
  std::vector<SubImageInfo> resolutions;

  SipiImgInfo() = default;
};

enum SipiCompressionParamName : std::uint8_t {
  JPEG_QUALITY,
  J2K_Sprofile,
  J2K_Creversible,
  J2K_Clayers,
  J2K_Clevels,
  J2K_Corder,
  J2K_Cprecincts,
  J2K_Cblk,
  J2K_Cuse_sop,
  J2K_Stiles,
  J2K_rates,
  TIFF_Pyramid,
};

using SipiCompressionParams = std::unordered_map<int, std::string>;

class SipiImage;//!< forward declaration of class SipiImage

/*!
 * This is the virtual base class for all classes implementing image I/O.
 */
class SipiIO
{
public:
  virtual ~SipiIO() = default;
  ;


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
  virtual bool read(SipiImage *img,
    const std::string &filepath,
    std::shared_ptr<SipiRegion> region,
    std::shared_ptr<SipiSize> size,
    bool force_bps_8,
    ScalingQuality scaling_quality) = 0;

  bool read(SipiImage *img, const std::string &filepath)
  {
    return read(img, filepath, nullptr, nullptr, false, { ScalingMethod::HIGH, ScalingMethod::HIGH, ScalingMethod::HIGH, ScalingMethod::HIGH });
  }

  bool read(SipiImage *img, const std::string &filepath, const std::shared_ptr<SipiRegion> region)
  {
    return read(img, filepath, region, nullptr, false, { ScalingMethod::HIGH, ScalingMethod::HIGH, ScalingMethod::HIGH, ScalingMethod::HIGH });
  }

  bool read(SipiImage *img,
    const std::string &filepath,
    const std::shared_ptr<SipiRegion> region,
    const std::shared_ptr<SipiSize> size)
  {
    return read(img, filepath, region, size, false, { ScalingMethod::HIGH, ScalingMethod::HIGH, ScalingMethod::HIGH, ScalingMethod::HIGH });
  }

  bool read(SipiImage *img,
    const std::string &filepath,
    const std::shared_ptr<SipiRegion> region,
    std::shared_ptr<SipiSize> size,
    bool force_bps_8)
  {
    return read(img, filepath, region, size, force_bps_8, { ScalingMethod::HIGH, ScalingMethod::HIGH, ScalingMethod::HIGH, ScalingMethod::HIGH });
  }

  /*!
   * Get the dimension of the image
   *
   * \param filepath Pathname of the image file
   */
  virtual SipiImgInfo getDim(const std::string &filepath) = 0;


  /*!
   * Write an image for a file using the given file format implemented by the subclass
   *
   * \param img Pointer to SipiImage instance
   * \param filepath Name of the image file to be written. Please note that
   * - "-" means to write the image data to stdout
   * - "HTTP" means to write the image data to the HTTP-server output
   * \param params Compression parameters
   */
  virtual void write(SipiImage *img, const std::string &filepath, const SipiCompressionParams *params) = 0;

  void write(SipiImage *img, const std::string &filepath) { write(img, filepath, nullptr); }
};
}// namespace Sipi

#endif
