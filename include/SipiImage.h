/*
 * Copyright © 2016 Lukas Rosenthaler, Andrea Bianco, Benjamin Geer,
 * Ivan Subotic, Tobias Schweizer, André Kilchenmann, and André Fatton.
 * This file is part of Sipi.
 * Sipi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * Sipi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Additional permission under GNU AGPL version 3 section 7:
 * If you modify this Program, or any covered work, by linking or combining
 * it with Kakadu (or a modified version of that library) or Adobe ICC Color
 * Profiles (or a modified version of that library) or both, containing parts
 * covered by the terms of the Kakadu Software Licence or Adobe Software Licence,
 * or both, the licensors of this Program grant you additional permission
 * to convey the resulting work.
 * See the GNU Affero General Public License for more details.
 * You should have received a copy of the GNU Affero General Public
 * License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.
 */
/*!
 * SipiImage is the core object of dealing with images within the Sipi package
 * The SipiImage object holds all the information about an image and offers the methods
 * to read, write and modify images. Reading and writing is supported in several standard formats
 * such as TIFF, J2k, PNG etc.
 */
#ifndef __sipi_image_h
#define __sipi_image_h

#include <sstream>
#include <utility>
#include <string>
#include <unordered_map>
#include <exception>
#include <string>

#include "SipiError.h"
#include "SipiIO.h"
#include "formats/SipiIOTiff.h"
#include "metadata/SipiXmp.h"
#include "metadata/SipiIcc.h"
#include "metadata/SipiIptc.h"
#include "metadata/SipiExif.h"
#include "metadata/SipiEssentials.h"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiSize.h"

#include "shttps/Connection.h"
#include "shttps/Hash.h"


/*!
 * \namespace Sipi Is used for all Sipi things.
 */
namespace Sipi {

    typedef unsigned char byte;
    typedef unsigned short word;

    /*! Implements the values of the photometric tag of the TIFF format */
    typedef enum : unsigned short {
        MINISWHITE = 0,     //!< B/W or gray value image with 0 = white and 1 (255) = black
        MINISBLACK = 1,     //!< B/W or gray value image with 0 = black and 1 (255) = white (is default in SIPI)
        RGB = 2,            //!< Color image with RGB values
        PALETTE = 3,        //!< Palette color image, is not suppoted by Sipi
        MASK = 4,           //!< Mask image, not supported by Sipi
        SEPARATED = 5,      //!< Color separated image, is assumed to be CMYK
        YCBCR = 6,          //!< Color representation with YCbCr, is supported by Sipi, but converted to an ordinary RGB
        CIELAB = 8,         //!< CIE*a*b image, only very limited support (untested!)
        ICCLAB = 9,         //!< ICCL*a*b image, only very limited support (untested!)
        ITULAB = 10,        //!< ITUL*a*b image, not supported yet (what is this by the way?)
        CFA = 32803,        //!< Color field array, used for DNG and RAW image. Not supported!
        LOGL = 32844,       //!< LOGL format (not supported)
        LOGLUV = 32845,     //!< LOGLuv format (not supported)
        LINEARRAW = 34892,  //!< Linear raw array for DNG and RAW formats. Not supported!
        INVALID = 65535     //!< an invalid value
    } PhotometricInterpretation;

    /*! The meaning of extra channels as used in the TIF format */
    typedef enum : unsigned short {
        UNSPECIFIED = 0,    //!< Unknown meaning
        ASSOCALPHA = 1,     //!< Associated alpha channel
        UNASSALPHA = 2      //!< Unassociated alpha channel
    } ExtraSamples;

    typedef enum {
        SKIP_NONE = 0x00, SKIP_ICC = 0x01, SKIP_XMP = 0x02, SKIP_IPTC = 0x04, SKIP_EXIF = 0x08, SKIP_ALL = 0xFF
    } SkipMetadata;

    enum InfoError { INFO_ERROR };

/*!
* This class implements the error handling for the different image formats.
*/
class SipiImageError final : public std::exception {
        std::string file; //!< Source file where the error occurs in
        int line; //!< Line within the source file
        int errnum; //!< error number if a system call is the reason for the error
        std::string errmsg;
        std::string fullerrmsg;

    public:
        /*!
        * Constructor
        * \param[in] file_p The source file name (usually __FILE__)
        * \param[in] line_p The line number in the source file (usually __LINE__)
        * \param[in] errnum_p if a unix system call is the reason for throwing this exception
        */
        SipiImageError(const char *file_p, const int line_p, const int errnum_p = 0) : file(file_p), line(line_p), errnum(errnum_p) {
            std::ostringstream errStream;
            errStream << "Sipi image error at [" << file << ": " << line << "]";
            if (errnum != 0) errStream << " (system error: " << std::strerror(errnum) << ")";
            errStream << ": " << errmsg;
            fullerrmsg = errStream.str();
        }

        /*!
        * Constructor
        * \param[in] file_p The source file name (usually __FILE__)
        * \param[in] line_p The line number in the source file (usually __LINE__)
        * \param[in] msg_p Error message describing the problem
        * \param[in] errnum_p Errnum, if a unix system call is the reason for throwing this exception
        */
        SipiImageError(const char *file_p, const int line_p, const char *msg_p, const int errnum_p = 0) : file(file_p), line(line_p), errnum(errnum_p), errmsg(msg_p) {
            std::ostringstream errStream;
            errStream << "Sipi image error at [" << file << ": " << line << "]";
            if (errnum != 0) errStream << " (system error: " << std::strerror(errnum) << ")";
            errStream << ": " << errmsg;
            fullerrmsg = errStream.str();
        }

        /*!
        * Constructor
        * \param[in] file_p The source file name (usually __FILE__)
        * \param[in] line_p The line number in the source file (usually __LINE__)
        * \param[in] msg_p Error message describing the problem
        * \param[in] errnum_p Errnum, if a unix system call is the reason for throwing this exception
        */
        SipiImageError(const char *file_p, const int line_p, std::string msg_p, const int errnum_p = 0) : file(file_p), line(line_p), errnum(errnum_p), errmsg(std::move(msg_p)) {
            std::ostringstream errStream;
            errStream << "Sipi image error at [" << file << ": " << line << "]";
            if (errnum != 0) errStream << " (system error: " << std::strerror(errnum) << ")";
            errStream << ": " << errmsg;
            fullerrmsg = errStream.str();
        }

        std::string to_string() const {
            std::ostringstream errStream;
            errStream << "Sipi image error at [" << file << ": " << line << "]";
            if (errnum != 0) errStream << " (system error: " << std::strerror(errnum) << ")";
            errStream << ": " << errmsg;
            return errStream.str();
        }
        //============================================================================

        const char* what() const noexcept override {
            return fullerrmsg.c_str();
        }

        friend std::ostream &operator<<(std::ostream &outStream, const SipiImageError &rhs) {
            const std::string errStr = rhs.to_string();
            outStream << errStr << std::endl; // TODO: remove the endl, the logging code should do it
            return outStream;
        }
        //============================================================================
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
    class SipiImage {
        friend class SipiIcc;       //!< We need SipiIcc as friend class
        friend class SipiIOTiff;    //!< I/O class for the TIFF file format
        friend class SipiIOJ2k;     //!< I/O class for the JPEG2000 file format
        //friend class SipiIOOpenJ2k; //!< I/O class for the JPEG2000 file format
        friend class SipiIOJpeg;    //!< I/O class for the JPEG file format
        friend class SipiIOPng;     //!< I/O class for the PNG file format
     private:
        static std::unordered_map<std::string, std::shared_ptr<SipiIO> > io; //!< member variable holding a map of I/O class instances for the different file formats
        static byte bilinn(byte buf[], register int nx, register double x, register double y, register int c, register int n);

        static word bilinn(word buf[], register int nx, register double x, register double y, register int c, register int n);

        void ensure_exif();

    protected:
        size_t nx;         //!< Number of horizontal pixels (width)
        size_t ny;         //!< Number of vertical pixels (height)
        size_t nc;         //!< Total number of samples per pixel
        size_t bps;        //!< bits per sample. Currently only 8 and 16 are supported
        std::vector<ExtraSamples> es; //!< meaning of extra samples
        Orientation orientation;
        PhotometricInterpretation photo;    //!< Image type, that is the meaning of the channels
        byte *pixels;   //!< Pointer to block of memory holding the pixels
        std::shared_ptr<SipiXmp> xmp;   //!< Pointer to instance SipiXmp class (\ref SipiXmp), or NULL
        std::shared_ptr<SipiIcc> icc;   //!< Pointer to instance of SipiIcc class (\ref SipiIcc), or NULL
        std::shared_ptr<SipiIptc> iptc; //!< Pointer to instance of SipiIptc class (\ref SipiIptc), or NULL
        std::shared_ptr<SipiExif> exif; //!< Pointer to instance of SipiExif class (\ref SipiExif), or NULL
        SipiEssentials emdata; //!< Metadata to be stored in file header
        shttps::Connection *conobj; //!< Pointer to HTTP connection
        SkipMetadata skip_metadata; //!< If true, all metadata is stripped off

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
         * Checks if the actual mimetype of an image file corresponds to the indicated mimetype and the extension of the filename.
         * This function is used to check if information submitted with a file are actually valid.
         */
         /* ToDo: Delete
        static bool checkMimeTypeConsistency(const std::string &path, const std::string &given_mimetype,
                                             const std::string &filename);
         */

        /*!
         * Getter for nx
         */
        inline size_t getNx() const { return nx; };


        /*!
         * Getter for ny
         */
        inline size_t getNy() const { return ny; };

        /*!
         * Getter for nc (includes alpha channels!)
         */
        inline size_t getNc() const { return nc; };

        /*!
         * Getter for number of alpha channels
         */
        inline size_t getNalpha() const { return es.size(); }

        /*!
         * Get bits per sample of image
         * @return bis per sample (bps)
         */
        inline size_t getBps() const { return bps; }

        inline std::shared_ptr<SipiExif> getExif() const { return exif; };


        /*!
         * Get orientation
         * @return Returns orientation tag
         */
        inline Orientation getOrientation() const { return orientation; };

        /*!
         * Set orientation parameter
         * @param ori orientation to be set
         */
        inline void setOrientation(Orientation ori) { orientation = ori; };


        /*!
         * Get photometric interpretation
         * @return Returns photometric interpretation tag
         */
         PhotometricInterpretation getPhoto() const { return photo; };


        /*! Destructor
         *
         * Destroys the image and frees all the resources associated with it
         */
        ~SipiImage();

        inline int getPixel(size_t x, size_t y, size_t c) {
            if (x >= nx) throw ((int) 1);
            if (y >= ny) throw ((int) 2);
            if (c >= nc) throw ((int) 3);
            switch (bps) {
                case 8: {
                    unsigned char *tmp = (unsigned char *) pixels;
                    return static_cast<int>(tmp[nc * (x * nx + y) + c]);
                }
                case 16: {
                    unsigned short *tmp = (unsigned short *) pixels;
                    return static_cast<int>(tmp[nc * (x * nx + y) + c]);
                }
                default: {
                    throw ((int) 6);
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
        inline void setPixel(size_t x, size_t y, size_t c, int val) {
            if (x >= nx) throw ((int) 1);
            if (y >= ny) throw ((int) 2);
            if (c >= nc) throw ((int) 3);

            switch (bps) {
                case 8: {
                    if (val > 0xff) throw ((int) 4);
                    unsigned char *tmp = (unsigned char *) pixels;
                    tmp[nc * (x * nx + y) + c] = (unsigned char) val;
                    break;
                }
                case 16: {
                    if (val > 0xffff) throw ((int) 5);
                    unsigned short *tmp = (unsigned short *) pixels;
                    tmp[nc * (x * nx + y) + c] = (unsigned short) val;
                    break;
                }
                default: {
                    if (val > 0xffff) throw ((int) 6);
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
        inline void setSkipMetadata(SkipMetadata smd) { skip_metadata = smd; };


        /*!
         * Stores the connection parameters of the shttps server in an Image instance
         *
         * \param[in] conn_p Pointer to connection data
         */
        inline void connection(shttps::Connection *conobj_p) { conobj = conobj_p; };

        /*!
         * Retrieves the connection parameters of the mongoose server from an Image instance
         *
         * \returns Pointer to connection data
         */
        inline shttps::Connection *connection() const { return conobj; };

        inline void essential_metadata(const SipiEssentials &emdata_p) { emdata = emdata_p; }

        inline SipiEssentials essential_metadata(void) { return emdata; }

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
        void read(const std::string &filepath, const std::shared_ptr<SipiRegion> &region = nullptr,
                  const std::shared_ptr<SipiSize> &size = nullptr, bool force_bps_8 = false,
                  ScalingQuality scaling_quality = {HIGH, HIGH, HIGH, HIGH});

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
        bool
        readOriginal(const std::string &filepath, const std::shared_ptr<SipiRegion> &region = nullptr, const std::shared_ptr<SipiSize> &size = nullptr,
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
        bool
        readOriginal(const std::string &filepath, const std::shared_ptr<SipiRegion> &region, const std::shared_ptr<SipiSize> &size,
                     const std::string &origname, shttps::HashType htype);


        /*!
         * Get the dimension of the image
         *
         * \param[in] filepath Pathname of the image file
         * \return Info about image (see SipiImgInfo)
         */
        SipiImgInfo getDim(const std::string &filepath) const;

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
        void removeExtraSamples() {
            const auto content_channels = (photo == SEPARATED ? 4 : 3);
            const auto extra_channels = static_cast<int>(es.size());
            for (size_t i = content_channels; i < (extra_channels + content_channels); i++)
                removeChan(i);
        }


        /*!
         * Removes a channel from a multi component image
         *
         * \param[in] chan Index of component to remove, starting with 0
         */
        void removeChan(unsigned int chan);

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

        bool operator==(const SipiImage &rhs);

        /*!
        * The overloaded << operator which is used to write the error message to the output
        *
        * \param[in] lhs The output stream
        * \param[in] rhs Reference to an instance of a SipiImage
        * \returns Returns ostream object
        */
        friend std::ostream &operator<<(std::ostream &lhs, const SipiImage &rhs);
    };
}

#endif
