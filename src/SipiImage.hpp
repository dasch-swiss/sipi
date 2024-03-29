/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * SipiImage is the core object of dealing with images within the Sipi package
 * The SipiImage object holds all the information about an image and offers the methods
 * to read, write and modify images. Reading and writing is supported in several standard formats
 * such as TIFF, J2k, PNG etc.
 */
#ifndef _sipi_image_h
#define _sipi_image_h

#include <string>
#include <unordered_map>

#include "../include/SipiIO.h"
#include "../include/iiifparser/SipiRegion.h"
#include "../include/iiifparser/SipiSize.h"
#include "../include/metadata/SipiEssentials.h"
#include "../include/metadata/SipiExif.h"
#include "../include/metadata/SipiIcc.h"
#include "../include/metadata/SipiIptc.h"
#include "../include/metadata/SipiXmp.h"

#include "../shttps/Connection.h"
#include "../shttps/Hash.h"


/*!
 * \namespace Sipi Is used for all Sipi things.
 */
namespace Sipi {

// Used for 8 bits per sample (color channel) images
using byte = unsigned char;

// Used for 16 bits per sample (color channel) images
using word = unsigned short;

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

inline auto to_string(const PhotometricInterpretation photo)-> std::string
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

/*! The meaning of extra channels as used in the TIF format */
enum class ExtraSamples : std::uint8_t {
  UNSPECIFIED = 0,//!< Unknown meaning
  ASSOCALPHA = 1,//!< Associated alpha channel
  UNASSALPHA = 2//!< Unassociated alpha channel
};

enum SkipMetadata : std::uint8_t {
  SKIP_NONE = 0x00,
  SKIP_ICC = 0x01,
  SKIP_XMP = 0x02,
  SKIP_IPTC = 0x04,
  SKIP_EXIF = 0x08,
  SKIP_ALL = 0xFF
};

enum InfoError { INFO_ERROR };

/*!
 * \class SipiImage
 *
 * Base class for all images in the Sipi package.
 * This class implements all the data and handling (methods) associated with
 * images in Sipi. Please note that the map of io-classes (see \ref SipiIO) has to
 * be instantiated in the SipiImage.cpp! Thus adding a new file format requires that SipiImage.cpp
 * is being modified!
 */
class SipiImage
{
  static std::unordered_map<std::string, std::shared_ptr<SipiIO>>
    io;//!< member variable holding a map of I/O class instances for the different file formats
  static byte bilinn(byte buf[], int nx, double x, double y, int c, int n);
  static word bilinn(word buf[], int nx, double x, double y, int c, int n);
  void ensure_exif();

protected:
  size_t nx;//!< Number of horizontal pixels (width)
  size_t ny;//!< Number of vertical pixels (height)
  size_t nc;//!< Total number of samples per pixel
  size_t bps;//!< bits per sample. Currently only 8 and 16 are supported
  std::vector<ExtraSamples> es;//!< meaning of the extra samples (channels)
  Orientation orientation;//!< Orientation of the image
  PhotometricInterpretation photo;//!< Image type, that is the meaning of the channels
  byte *pixels;//!< Pointer to block of memory holding the pixels (allways in big-endian format if interpreted as 16
               //!< bit/sample)
  std::shared_ptr<SipiXmp> xmp;//!< Pointer to instance SipiXmp class (\ref SipiXmp), or NULL
  std::shared_ptr<SipiIcc> icc;//!< Pointer to instance of SipiIcc class (\ref SipiIcc), or NULL
  std::shared_ptr<SipiIptc> iptc;//!< Pointer to instance of SipiIptc class (\ref SipiIptc), or NULL
  std::shared_ptr<SipiExif> exif;//!< Pointer to instance of SipiExif class (\ref SipiExif), or NULL
  SipiEssentials emdata;//!< Metadata to be stored in file header
  shttps::Connection *conobj;//!< Pointer to HTTP connection
  SkipMetadata skip_metadata;//!< If true, all metadata is stripped off

public:
  //
  /*!
   * Default constructor. Creates an empty image
   */
  SipiImage();

  /*!
   * Copy constructor. Makes a deep copy of the image
   *
   * \param[in] img_p An existing instance if SipiImage
   */
  SipiImage(const SipiImage &img_p);

  /*!
   * Create an empty image with the pixel buffer available, but all pixels set to 0
   *
   * \param[in] nx_p Dimension in x direction
   * \param[in] ny_p Dimension in y direction
   * \param[in] nc_p Number of channels
   * \param[in] bps_p Bits per sample, either 8 or 16 are allowed
   * \param[in] photo_p The photometric interpretation
   */
  SipiImage(size_t nx_p, size_t ny_p, size_t nc_p, size_t bps_p, PhotometricInterpretation photo_p);

  /*!
   * Getter for nx
   */
  [[nodiscard]] size_t getNx() const { return nx; };

  /*!
   * Getter for ny
   */
  [[nodiscard]] size_t getNy() const { return ny; };

  /*!
   * Getter for nc (includes alpha channels!)
   */
  [[nodiscard]] size_t getNc() const { return nc; };

  /*!
   * Getter for number of alpha channels
   */
  [[nodiscard]] size_t getNalpha() const { return es.size(); }

  /*!
   * Get bits per sample of image
   * @return bis per sample (bps)
   */
  [[nodiscard]] size_t getBps() const { return bps; }

  /**
   * Get the exif metadata of the image.
   * \return exif metadata
   */
  [[nodiscard]] std::shared_ptr<SipiExif> getExif() const { return exif; };

  /*!
   * Get orientation
   * @return Returns orientation tag
   */
  [[nodiscard]] Orientation getOrientation() const { return orientation; };

  /*!
   * Set orientation parameter
   * @param value orientation value to be set
   */
  void setOrientation(Orientation value) { orientation = value; };


  /*!
   * Get photometric interpretation
   * @return Returns photometric interpretation tag
   */
  [[nodiscard]] PhotometricInterpretation getPhoto() const { return photo; };


  /*! Destructor
   *
   * Destroys the image and frees all the resources associated with it
   */
  ~SipiImage();

  int getPixel(size_t x, size_t y, size_t c)
  {
    if (x >= nx) throw((int)1);
    if (y >= ny) throw((int)2);
    if (c >= nc) throw((int)3);
    switch (bps) {
    case 8: {
      unsigned char *tmp = (unsigned char *)pixels;
      return static_cast<int>(tmp[nc * (x * nx + y) + c]);
    }
    case 16: {
      unsigned short *tmp = (unsigned short *)pixels;
      return static_cast<int>(tmp[nc * (x * nx + y) + c]);
    }
    default: {
      throw((int)6);
    }
    }
  }

  /*!
   * Sets a pixel to a given value
   *
   * \param[in] x X position
   * \param[in] y Y position
   * \param[in] c Color channels
   * \param[in] val Pixel value
   */
  void setPixel(size_t x, size_t y, size_t c, int val)
  {
    if (x >= nx) throw((int)1);
    if (y >= ny) throw((int)2);
    if (c >= nc) throw((int)3);

    switch (bps) {
    case 8: {
      if (val > 0xff) throw((int)4);
      unsigned char *tmp = (unsigned char *)pixels;
      tmp[nc * (x * nx + y) + c] = (unsigned char)val;
      break;
    }
    case 16: {
      if (val > 0xffff) throw((int)5);
      unsigned short *tmp = (unsigned short *)pixels;
      tmp[nc * (x * nx + y) + c] = (unsigned short)val;
      break;
    }
    default: {
      if (val > 0xffff) throw((int)6);
    }
    }
  }

  /*!
   * Assignment operator
   *
   * Makes a deep copy of the instance
   *
   * \param[in] img_p Instance of a SipiImage
   */
  SipiImage &operator=(const SipiImage &img_p);

  /*!
   * Set the metadata that should be skipped in writing a file
   *
   * \param[in] smd Logical "or" of bitmasks for metadata to be skipped
   */
  void setSkipMetadata(SkipMetadata smd) { skip_metadata = smd; };


  /*!
   * Stores the connection parameters of the shttps server in an Image instance
   *
   * \param[in] conn_p Pointer to connection data
   */
  void connection(shttps::Connection *conobj_p) { conobj = conobj_p; };

  /*!
   * Retrieves the connection parameters of the mongoose server from an Image instance
   *
   * \returns Pointer to connection data
   */
  [[nodiscard]] shttps::Connection *connection() const { return conobj; };

  void essential_metadata(const SipiEssentials &emdata_p) { emdata = emdata_p; }

  [[nodiscard]] SipiEssentials essential_metadata() const { return emdata; }

  /*!
   * Read an image from the given path
   *
   * \param[in] filepath A string containing the path to the image file
   * \param[in] region Pointer to a SipiRegion which indicates that we
   *            are only interested in this region. The image will be cropped.
   * \param[in] size Pointer to a size object. The image will be scaled accordingly
   * \param[in] force_bps_8 We want in any case a 8 Bit/sample image. Reduce if necessary. Default is false.
   * \param[in] scaling_quality Quality of the scaling algorithm. Default is HIGH.
   *
   * \throws SipiError
   */
  void read(const std::string &filepath,
    const std::shared_ptr<SipiRegion> &region = nullptr,
    const std::shared_ptr<SipiSize> &size = nullptr,
    bool force_bps_8 = false,
    ScalingQuality scaling_quality = { ScalingMethod::HIGH,
      ScalingMethod::HIGH,
      ScalingMethod::HIGH,
      ScalingMethod::HIGH });

  /*!
   * Read an image that is to be considered an "original image". In this case
   * a SipiEssentials object is created containing the original name, the
   * original mime type. In addition also a checksum of the pixel values
   * is added in order to guarantee the integrity of the image pixels.
   * if the image is written as J2K or as TIFF image, these informations
   * are added to the file header (in case of TIFF as a private tag 65111,
   * in case of J2K as comment box).
   * If the file read already contains a SipiEssentials as embedded metadata,
   * it is not overwritten, put the embedded and pixel checksums are compared.
   *
   * \param[in] filepath A string containing the path to the image file
   * \param[in] region Pointer to a SipiRegion which indicates that we
   *            are only interested in this region. The image will be cropped.
   * \param[in] size Pointer to a size object. The image will be scaled accordingly
   * \param[in] htype The checksum method that should be used if the checksum is
   *            being calculated for the first time.
   *
   * \returns true, if everything worked. False, if the checksums do not match.
   */
  bool readOriginal(const std::string &filepath,
    const std::shared_ptr<SipiRegion> &region = nullptr,
    const std::shared_ptr<SipiSize> &size = nullptr,
    shttps::HashType htype = shttps::HashType::sha256);

  /*!
   * Read an image that is to be considered an "original image". In this case
   * a SipiEssentials object is created containing the original name, the
   * original mime type. In addition also a checksum of the pixel values
   * is added in order to guarantee the integrity of the image pixels.
   * if the image is written as J2K or as TIFF image, these informations
   * are added to the file header (in case of TIFF as a private tag 65111,
   * in case of J2K as comment box).
   * If the file read already contains a SipiEssentials as embedded metadata,
   * it is not overwritten, put the embedded and pixel checksums are compared.
   *
   * \param[in] filepath A string containing the path to the image file
   * \param[in] region Pointer to a SipiRegion which indicates that we
   *            are only interested in this region. The image will be cropped.
   * \param[in] size Pointer to a size object. The image will be scaled accordingly
   * \param[in] origname Original file name
   * \param[in] htype The checksum method that should be used if the checksum is
   *            being calculated for the first time.
   *
   * \returns true, if everything worked. False, if the checksums do not match.
   */
  bool readOriginal(const std::string &filepath,
    const std::shared_ptr<SipiRegion> &region,
    const std::shared_ptr<SipiSize> &size,
    const std::string &origname,
    shttps::HashType htype);


  /*!
   * Get the dimension of the image
   *
   * \param[in] filepath Pathname of the image file
   * \return Info about image (see SipiImgInfo)
   */
  [[nodiscard]] SipiImgInfo getDim(const std::string &filepath) const;

  /*!
   * Get the dimension of the image object
   *
   * @param[out] width Width of the image in pixels
   * @param[out] height Height of the image in pixels
   */
  void getDim(size_t &width, size_t &height) const;

  /*!
   * Write an image to somewhere
   *
   * This method writes the image to a destination. The destination can be
   * - a file if w path (filename) is given
   * - stdout of the filepath is "-"
   * - to the websocket, if the filepath is the string "HTTP" (given the webserver is activated)
   *
   * \param[in] ftype The file format that should be used to write the file. Supported are
   * - "tif" for TIFF files
   * - "j2k" for JPEG2000 files
   * - "png" for PNG files
   * \param[in] filepath String containing the path/filename
   */
  void write(const std::string &ftype, const std::string &filepath, const SipiCompressionParams *params = nullptr);


  /*!
   * Convert full range YCbCr (YCC) to RGB colors
   */
  void convertYCC2RGB();


  /*!
   * Converts the image representation
   *
   * \param[in] target_icc_p ICC profile which determines the new image representation
   * \param[in] bps Bits/sample of the new image representation
   */
  void convertToIcc(const SipiIcc &target_icc_p, int bps);


  /*!
   * Remove extra samples from the image. Some output formats support only 3 channels (e.g., JPEG)
   * so we need to remove the alpha channel. If we are dealing with a CMYK image, we need to take
   * this into account as well.
   */
  void removeExtraSamples(const bool force_gray_alpha = false)
  {
    const size_t content_channels = (photo == PhotometricInterpretation::SEPARATED ? 4 : 3);
    const size_t extra_channels = es.size();
    for (size_t i = content_channels; i < (extra_channels + content_channels); i++) {
      removeChannel(i, force_gray_alpha);
    }
  }


  /*!
   * Removes a channel from a multi component image
   *
   * \param[in] channel Index of component to remove, starting with 0
   * \param[in] force_gray_alpha If true,  based on the alpha channel that is removed, a gray value is applied
   * to the remaining channels. This is useful for image formats that don't support alpha channel and where the
   * main content is black, so it is better separated from the background (as the default would be black).
   */
  void removeChannel(unsigned int channel, bool force_gray_alpha = false);

  /*!
   * Crops an image to a region
   *
   * \param[in] x Horizontal start position of region. If negative, it's set to 0, and the width is adjusted
   * \param[in] y Vertical start position of region. If negative, it's set to 0, and the height is adjusted
   * \param[in] width Width of the region. If the region goes beyond the image dimensions, it's adjusted.
   * \param[in] height Height of the region. If the region goes beyond the image dimensions, it's adjusted
   */
  bool crop(int x, int y, size_t width = 0, size_t height = 0);

  /*!
   * Crops an image to a region
   *
   * \param[in] Pointer to SipiRegion
   * \param[in] ny Vertical start position of region. If negative, it's set to 0, and the height is adjusted
   * \param[in] width Width of the region. If the region goes beyond the image dimensions, it's adjusted.
   * \param[in] height Height of the region. If the region goes beyond the image dimensions, it's adjusted
   */
  bool crop(const std::shared_ptr<SipiRegion> &region);

  /*!
   * Resize an image using a high speed algorithm which may result in poor image quality
   *
   * \param[in] nnx New horizontal dimension (width)
   * \param[in] nny New vertical dimension (height)
   */
  bool scaleFast(size_t nnx, size_t nny);

  /*!
   * Resize an image using some balance between speed and quality
   *
   * \param[in] nnx New horizontal dimension (width)
   * \param[in] nny New vertical dimension (height)
   */
  bool scaleMedium(size_t nnx, size_t nny);

  /*!
   * Resize an image using the best (but slow) algorithm
   *
   * \param[in] nnx New horizontal dimension (width)
   * \param[in] nny New vertical dimension (height)
   */
  bool scale(size_t nnx = 0, size_t nny = 0);


  /*!
   * Rotate an image
   *
   * The angles 0, 90, 180, 270 are treated specially!
   *
   * \param[in] angle Rotation angle
   * \param[in] mirror If true, mirror the image before rotation
   */
  bool rotate(float angle, bool mirror = false);

  /*!
   * Rotate the image if necessare so that ot has TOPLEFT orientation
   *
   * @return Returns true on success, false on error
   */
  bool set_topleft();

  /*!
   * Convert an image from 16 to 8 bit. The algorithm just divides all pixel values
   * by 256 using the ">> 8" operator (fast & efficient)
   *
   * \returns Returns true on success, false on error
   */
  bool to8bps();

  /*!
   * Convert an image to a bitonal representation using Steinberg-Floyd dithering.
   *
   * The method does nothing if the image is already bitonal. Otherwise, the image is converted
   * into a gray value image if necessary and then a FLoyd-Steinberg dithering is applied.
   *
   * \returns Returns true on success, false on error
   */
  bool toBitonal();

  /*!
   * Add a watermark to a file...
   *
   * \param[in] wmfilename Path to watermarkfile (which must be a TIFF file at the moment)
   */
  void add_watermark(const std::string &wmfilename);


  /*!
   * Calculates the difference between 2 images.
   *
   * The difference between 2 images can contain (and usually will) negative values.
   * In order to create a standard image, the values at "0" will be lifted to 127 (8-bit images)
   * or 32767. The span will be defined by max(minimum, maximum), where minimum and maximum are
   * absolute values. Thus a new pixelvalue will be calculated as follows:
   * ```
   * int maxmax = abs(min) > abs(max) ? abs(min) : abs(min);
   * newval = (byte) ((oldval + maxmax)*UCHAR_MAX/(2*maxmax));
   * ```
   * \param[in] rhs right hand side of "-="
   */
  SipiImage &operator-=(const SipiImage &rhs);

  /*!
   * Calculates the difference between 2 images.
   *
   * The difference between 2 images can contain (and usually will) negative values.
   * In order to create a standard image, the values at "0" will be lifted to 127 (8-bit images)
   * or 32767. The span will be defined by max(minimum, maximum), where minimum and maximum are
   * absolute values. Thus a new pixelvalue will be calculated as follows:
   * ```
   * int maxmax = abs(min) > abs(max) ? abs(min) : abs(min);
   * newval = (byte) ((oldval + maxmax)*UCHAR_MAX/(2*maxmax));
   * ```
   *
   * \param[in] lhs left-hand side of "-" operator
   * \param[in] rhs right hand side of "-" operator
   */
  SipiImage &operator-(const SipiImage &rhs);

  SipiImage &operator+=(const SipiImage &rhs);

  SipiImage &operator+(const SipiImage &rhs);

  bool operator==(const SipiImage &rhs) const;

  /*!
   * The overloaded << operator which is used to write the error message to the output
   *
   * \param[in] lhs The output stream
   * \param[in] rhs Reference to an instance of a SipiImage
   * \returns Returns ostream object
   */
  friend std::ostream &operator<<(std::ostream &lhs, const SipiImage &rhs);

  friend class SipiIcc;//!< We need SipiIcc as friend class
  friend class SipiIOTiff;//!< I/O class for the TIFF file format
  friend class SipiIOJ2k;//!< I/O class for the JPEG2000 file format
  friend class SipiIOJpeg;//!< I/O class for the JPEG file format
  friend class SipiIOPng;//!< I/O class for the PNG file format
};
}// namespace Sipi

#endif
