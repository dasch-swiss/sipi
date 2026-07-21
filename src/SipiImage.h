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
// #include <unordered_map>

#include "SipiIO.h"
// #include "iiifparser/SipiRegion.h"
#include "metadata/essentials.h"
#include "metadata/exif.h"
#include "metadata/icc.h"
#include "metadata/iptc.h"
#include "metadata/photometric_interpretation.h"
#include "metadata/xmp.h"


/*!
 * \namespace Sipi Is used for all Sipi things.
 */
namespace Sipi {

// Used for 8 bits per sample (color channel) images
using byte = unsigned char;

// Used for 16 bits per sample (color channel) images
using word = unsigned short;

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
 * \struct PixelDelta
 *
 * Per-channel absolute pixel-difference statistics between two images,
 * as reported by the `sipi compare` command. `max_abs` is the largest
 * |sample₁ − sample₂| across all pixels and channels, located at
 * (`max_x`, `max_y`); `mean_abs` is the mean |Δ| over every sample.
 *
 * `max_x`/`max_y` are row-major (`y * nx + x`) pixel coordinates, matching
 * the layout used by `operator-=`, the format handlers, and
 * `getPixel`/`setPixel` — so they may be fed back into those accessors.
 */
struct PixelDelta
{
  double mean_abs;//!< mean absolute per-channel difference
  int max_abs;//!< maximum absolute per-channel difference
  size_t max_x;//!< x coordinate of the maximum absolute difference (codec-store order)
  size_t max_y;//!< y coordinate of the maximum absolute difference (codec-store order)
};

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
  static byte bilinn(byte buf[], int nx, int ny, double x, double y, int c, int n);
  static word bilinn(word buf[], int nx, int ny, double x, double y, int c, int n);
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
  std::shared_ptr<Xmp> xmp;//!< Pointer to instance Xmp class (\ref Xmp), or NULL
  std::shared_ptr<Icc> icc;//!< Pointer to instance of Icc class (\ref Icc), or NULL
  std::shared_ptr<Iptc> iptc;//!< Pointer to instance of Iptc class (\ref Iptc), or NULL
  std::shared_ptr<Exif> exif;//!< Pointer to instance of Exif class (\ref Exif), or NULL
  Essentials emdata;//!< Metadata to be stored in file header
  SkipMetadata skip_metadata;//!< If true, all metadata is stripped off

  /*!
   * Adobe APP14 JPEG marker transform flag. Encodes the Photoshop "Unknown"
   * convention for CMYK/YCCK polarity:
   *   255 = no APP14 marker present (default; raw CMYK or non-JPEG formats)
   *     0 = Adobe "Unknown / CMYK" — when nc==4, libjpeg-turbo produces
   *         inverted CMYK that SipiIOJpeg re-inverts before ICC conversion
   *     1 = YCbCr (converted to RGB by libjpeg-turbo — no inversion)
   *     2 = YCCK (libjpeg-turbo produces inverted CMYK that must be re-inverted)
   * Only read/written by the JPEG handler; default-initialized to 255 so all
   * non-JPEG paths treat images as "no APP14" and skip the inversion branch.
   */
  uint8_t app14_transform = 255;

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
   * Move constructor. Transfers ownership of pixel buffer and metadata.
   */
  SipiImage(SipiImage &&other) noexcept;

  /*!
   * Move assignment operator. Transfers ownership of pixel buffer and metadata.
   */
  SipiImage &operator=(SipiImage &&other) noexcept;

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
  [[nodiscard]] std::shared_ptr<Exif> getExif() const { return exif; };

  /**
   * Get the ICC color profile of the image.
   * \return ICC profile, or nullptr if not set
   */
  [[nodiscard]] std::shared_ptr<Icc> getIcc() const { return icc; }

  /**
   * Get the XMP metadata of the image.
   * \return XMP metadata, or nullptr if not set
   */
  [[nodiscard]] std::shared_ptr<Xmp> getXmp() const { return xmp; }

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

  /*!
   * Gets the value of a pixel sample.
   *
   * Indexes the pixel buffer row-major (`nc * (y * nx + x) + c`), matching the
   * codec store written by the format handlers and read by `maxPixelDelta`.
   *
   * \param[in] x X position
   * \param[in] y Y position
   * \param[in] c Color channel
   * \return The sample value
   */
  [[nodiscard]] int getPixel(size_t x, size_t y, size_t c)
  {
    if (x >= nx) throw((int)1);
    if (y >= ny) throw((int)2);
    if (c >= nc) throw((int)3);
    switch (bps) {
    case 8: {
      unsigned char *tmp = (unsigned char *)pixels;
      return static_cast<int>(tmp[nc * (y * nx + x) + c]);
    }
    case 16: {
      unsigned short *tmp = (unsigned short *)pixels;
      return static_cast<int>(tmp[nc * (y * nx + x) + c]);
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
      tmp[nc * (y * nx + x) + c] = (unsigned char)val;
      break;
    }
    case 16: {
      if (val > 0xffff) throw((int)5);
      unsigned short *tmp = (unsigned short *)pixels;
      tmp[nc * (y * nx + x) + c] = (unsigned short)val;
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

  void essential_metadata(const Essentials &emdata_p) { emdata = emdata_p; }

  [[nodiscard]] Essentials essential_metadata() const { return emdata; }

  /*!
   * Hash the current pixel buffer with the requested digest algorithm and
   * return the raw digest bytes (NOT hex). Used by the `convert service-file`
   * command (DEV-6540) to
   * populate `EssentialsFields::data_chksum` over the **post-transformation**
   * pixel buffer, and by the corruption-tripwire branch in `readSource`
   * (which compares against the on-disk `data_chksum` from an existing
   * Essentials packet — same digest, hex-vs-raw bridged via
   * `Essentials::to_hex`).
   *
   * Buffer layout matches `readSource`'s tripwire: `nx * ny * nc * bps / 8`
   * raw bytes, big-endian when `bps == 16`.
   */
  [[nodiscard]] std::vector<std::byte> compute_pixel_hash(shttps::HashType type) const;

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
   * Read an image from disk into memory. The tool makes no claim that the file
   * is "the original" — it is the source for the current operation (ADR-0009,
   * ADR-0010, DEV-6539).
   *
   * If the source happens to be a Service File carrying an Essentials packet,
   * the embedded pixel checksum is recomputed and compared against the
   * `data_chksum` field as a corruption tripwire (ADR-0010): on mismatch, an
   * ERROR is logged and reading continues. No Essentials packet is *created*
   * during read; packet creation is intentional output gated by the
   * `convert service-file` subcommand (DEV-6540).
   *
   * \param[in] filepath A string containing the path to the source image file
   * \param[in] region Optional region of interest — the image will be cropped
   * \param[in] size Optional size — the image will be scaled accordingly
   */
  void readSource(const std::string &filepath,
    const std::shared_ptr<SipiRegion> &region = nullptr,
    const std::shared_ptr<SipiSize> &size = nullptr);

  /*!
   * Overload accepting an `origname` hint. Until the `convert service-file`
   * command lands (DEV-6540), `origname` is consumed by the
   * command — not by readSource itself — so this overload behaves
   * identically to the 3-arg form. Kept for the existing Lua-side call site.
   */
  void readSource(const std::string &filepath,
    const std::shared_ptr<SipiRegion> &region,
    const std::shared_ptr<SipiSize> &size,
    const std::string &origname);


  /*!
   * Read the image shape (dimensions, tiling, levels, channels, bit depth) from a file
   * without performing a full decode. Dispatches to the format handler's read_shape;
   * service-file handlers may take a fast path via the Essentials packet (ADR-0004).
   *
   * \param[in] filepath Pathname of the image file
   * \return Info about image (see SipiImgInfo)
   */
  [[nodiscard]] SipiImgInfo read_shape(const std::string &filepath) const;

  /*!
   * Get the dimensions of an in-memory SipiImage (already loaded; no file I/O).
   *
   * @param[out] width Width of the image in pixels
   * @param[out] height Height of the image in pixels
   */
  void getDim(size_t &width, size_t &height) const;

  /*!
   * Write an image to the given OutputSink (ADR-0006).
   *
   * \param[in] ftype The file format that should be used to write the file. Supported are
   * the keys of the static SipiIO handler map (SipiImage.cpp):
   * - "tif" for TIFF files
   * - "jpx" for JPEG2000 files
   * - "png" for PNG files
   * - "jpg" for JPEG files
   * Any other value throws std::out_of_range.
   * \param[in] sink Where the encoded bytes go: a FilePath (file, or stdout via
   * "-"/"stdout:"), a CallbackSink, or a TeeSink.
   */
  void write(const std::string &ftype, const OutputSink &sink, const SipiCompressionParams *params = nullptr);

  /*!
   * Convenience overload: write to a filesystem path (or stdout via "-" /
   * "stdout:"). Equivalent to write(ftype, FilePath{filepath}, params).
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
  void convertToIcc(const Icc &target_icc_p, int bps);


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
   * Conclude similarity of two SipiImages, used in tests. Only tested with small differences.
   *
   * \returns Returns similarity index, returns in [0..1]
   */
  std::optional<double> compare(const SipiImage &rhs) const;

  /*!
   * Computes the per-channel absolute pixel-difference statistics against
   * another image. Unlike `operator-=` (which rescales the signed diff into
   * a displayable visualization), this reads the raw samples and reports the
   * true mean and maximum |Δ| plus the location of the maximum. Used by
   * `sipi compare` as the codec-rebaseline tolerance metric.
   *
   * \param[in] rhs image to compare against
   * \returns the difference statistics, or nullopt if the images are not
   *          comparable (differing dimensions, channels, bit depth, or photometric interpretation)
   */
  [[nodiscard]] std::optional<PixelDelta> maxPixelDelta(const SipiImage &rhs) const;

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
  SipiImage operator-(const SipiImage &rhs) const;

  SipiImage &operator+=(const SipiImage &rhs);

  SipiImage operator+(const SipiImage &rhs) const;

  bool operator==(const SipiImage &rhs) const;

  /*!
   * The overloaded << operator which is used to write the error message to the output
   *
   * \param[in] lhs The output stream
   * \param[in] rhs Reference to an instance of a SipiImage
   * \returns Returns ostream object
   */
  friend std::ostream &operator<<(std::ostream &lhs, const SipiImage &rhs);

  friend class SipiIOTiff;//!< I/O class for the TIFF file format
  friend class SipiIOJ2k;//!< I/O class for the JPEG2000 file format
  friend class SipiIOJpeg;//!< I/O class for the JPEG file format
  friend class SipiIOPng;//!< I/O class for the PNG file format
};
}// namespace Sipi

#endif
