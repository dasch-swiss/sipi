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
#include <iostream>
#include <string>
#include <vector>
#include <cmath>

//#include <memory>
#include <climits>

#include "lcms2.h"
#include "makeunique.h"

#include "shttps/Global.h"
#include "shttps/Hash.h"
#include "SipiImage.h"
#include "formats/SipiIOTiff.h"
#include "formats/SipiIOJ2k.h"
//#include "formats/SipiIOOpenJ2k.h"
#include <sys/stat.h>

#include "formats/SipiIOJpeg.h"
#include "formats/SipiIOPng.h"
#include "shttps/Parsing.h"

static const char __file__[] = __FILE__;


namespace Sipi {

    std::unordered_map<std::string, std::shared_ptr<SipiIO> > SipiImage::io = {{"tif", std::make_shared<SipiIOTiff>()},
                                                                               {"jpx", std::make_shared<SipiIOJ2k>()},
            //{"jpx", std::make_shared<SipiIOOpenJ2k>()},
                                                                               {"jpg", std::make_shared<SipiIOJpeg>()},
                                                                               {"png", std::make_shared<SipiIOPng>()}};

    SipiImage::SipiImage() {
        nx = 0;
        ny = 0;
        nc = 0;
        bps = 0;
        photo = INVALID;
        orientation = TOPLEFT;
        pixels = nullptr;
        xmp = nullptr;
        icc = nullptr;
        iptc = nullptr;
        exif = nullptr;
        skip_metadata = SKIP_NONE;
        conobj = nullptr;
    };
    //============================================================================

    SipiImage::SipiImage(const SipiImage &img_p) {
        nx = img_p.nx;
        ny = img_p.ny;
        nc = img_p.nc;
        bps = img_p.bps;
        es = img_p.es;
        orientation = img_p.orientation;
        photo = img_p.photo;
        size_t bufsiz;

        switch (bps) {
            case 8: {
                bufsiz = nx * ny * nc * sizeof(unsigned char);
                break;
            }

            case 16: {
                bufsiz = nx * ny * nc * sizeof(unsigned short);
                break;
            }

            default: {
                bufsiz = 0;
            }
        }

        if (bufsiz > 0) {
            pixels = new byte[bufsiz];
            memcpy(pixels, img_p.pixels, bufsiz);
        }

        xmp = std::make_shared<SipiXmp>(*img_p.xmp);
        icc = std::make_shared<SipiIcc>(*img_p.icc);
        iptc = std::make_shared<SipiIptc>(*img_p.iptc);
        exif = std::make_shared<SipiExif>(*img_p.exif);
        emdata = img_p.emdata;
        skip_metadata = img_p.skip_metadata;
        conobj = img_p.conobj;
    }
    //============================================================================

    SipiImage::SipiImage(size_t nx_p, size_t ny_p, size_t nc_p, size_t bps_p, PhotometricInterpretation photo_p) : nx(
            nx_p), ny(ny_p), nc(nc_p), bps(bps_p), photo(photo_p) {
        orientation = TOPLEFT; // assuming default...
        if (((photo == MINISWHITE) || (photo == MINISBLACK)) && !((nc == 1) || (nc == 2))) {
            throw SipiImageError(__file__, __LINE__, "Mismatch in Photometric interpretation and number of channels");
        }

        if ((photo == RGB) && !((nc == 3) || (nc == 4))) {
            throw SipiImageError(__file__, __LINE__, "Mismatch in Photometric interpretation and number of channels");
        }

        if ((bps != 8) && (bps != 16)) {
            throw SipiImageError(__file__, __LINE__, "Bits per samples not supported by Sipi");
        }

        size_t bufsiz;

        switch (bps) {
            case 8: {
                bufsiz = nx * ny * nc * sizeof(unsigned char);
                break;
            }

            case 16: {
                bufsiz = nx * ny * nc * sizeof(unsigned short);
                break;
            }

            default: {
                bufsiz = 0;
            }
        }

        if (bufsiz > 0) {
            pixels = new byte[bufsiz];
        } else {
            throw SipiImageError(__file__, __LINE__, "Image with no content");
        }

        xmp = nullptr;
        icc = nullptr;
        iptc = nullptr;
        exif = nullptr;
        skip_metadata = SKIP_NONE;
        conobj = nullptr;
    }
    //============================================================================

    SipiImage::~SipiImage() {
        delete[] pixels;
    }
    //============================================================================


    SipiImage &SipiImage::operator=(const SipiImage &img_p) {
        if (this != &img_p) {
            nx = img_p.nx;
            ny = img_p.ny;
            nc = img_p.nc;
            bps = img_p.bps;
            orientation = img_p.orientation;
            es = img_p.es;
            size_t bufsiz;

            switch (bps) {
                case 8: {
                    bufsiz = nx * ny * nc * sizeof(unsigned char);
                    break;
                }

                case 16: {
                    bufsiz = nx * ny * nc * sizeof(unsigned short);
                    break;
                }

                default: {
                    bufsiz = 0;
                }
            }

            if (bufsiz > 0) {
                pixels = new byte[bufsiz];
                memcpy(pixels, img_p.pixels, bufsiz);
            }

            xmp = std::make_shared<SipiXmp>(*img_p.xmp);
            icc = std::make_shared<SipiIcc>(*img_p.icc);
            iptc = std::make_shared<SipiIptc>(*img_p.iptc);
            exif = std::make_shared<SipiExif>(*img_p.exif);
            skip_metadata = img_p.skip_metadata;
            conobj = img_p.conobj;
        }

        return *this;
    }
    //============================================================================

    /*!
     * If this image has no SipiExif, creates an empty one.
     */
    void SipiImage::ensure_exif() {
        if (exif == nullptr) exif = std::make_shared<SipiExif>();
    }
    //============================================================================


    /*!
     * Reads the image from a file by calling the appropriate reader.
     * The readers return either boolean or throw an exception,
     * so in any case wrap the call to this method in a try/catch block.
     */
    void SipiImage::read(const std::string& filepath, const std::shared_ptr<SipiRegion>& region,
                         const std::shared_ptr<SipiSize>& size, bool force_bps_8, ScalingQuality scaling_quality) {
        size_t pos = filepath.find_last_of('.');
        std::string fext = filepath.substr(pos + 1);
        std::string _fext;

        bool got_file = false;
        _fext.resize(fext.size());
        std::transform(fext.begin(), fext.end(), _fext.begin(), ::tolower);

        if ((_fext == "tif") || (_fext == "tiff")) {
            got_file = io[std::string("tif")]->read(this, filepath, region, size, force_bps_8, scaling_quality);
        } else if ((_fext == "jpg") || (_fext == "jpeg")) {
            got_file = io[std::string("jpg")]->read(this, filepath, region, size, force_bps_8, scaling_quality);
        } else if (_fext == "png") {
            got_file = io[std::string("png")]->read(this, filepath, region, size, force_bps_8, scaling_quality);
        } else if ((_fext == "jp2") || (_fext == "jpx") || (_fext == "j2k")) {
            got_file = io[std::string("jpx")]->read(this, filepath, region, size, force_bps_8, scaling_quality);
        }

        if (!got_file) {
            for (auto const &iterator : io) {
                if ((got_file = iterator.second->read(this, filepath, region, size, force_bps_8, scaling_quality))) break;
            }
        }

        if (!got_file) {
            throw SipiImageError(__file__, __LINE__, "Error reading file " + filepath);
        }
    }
    //============================================================================

    bool SipiImage::readOriginal(const std::string &filepath, const std::shared_ptr<SipiRegion>& region,
                                 const std::shared_ptr<SipiSize>& size, shttps::HashType htype) {
        read(filepath, region, size, false);

        if (!emdata.is_set()) {
            shttps::Hash internal_hash(htype);
            internal_hash.add_data(pixels, nx * ny * nc * bps / 8);
            std::string checksum = internal_hash.hash();
            std::string origname = shttps::getFileName(filepath);
            std::string mimetype = shttps::Parsing::getFileMimetype(filepath).first;
            std::vector<unsigned char> iccprofile;
            if (icc != nullptr) {
                iccprofile = icc->iccBytes();
            }
            SipiEssentials emdata2(origname, mimetype, shttps::HashType::sha256, checksum, iccprofile);
            essential_metadata(emdata2);
        } else {
            shttps::Hash internal_hash(emdata.hash_type());
            internal_hash.add_data(pixels, nx * ny * nc * bps / 8);
            std::string checksum = internal_hash.hash();
            if (checksum != emdata.data_chksum()) {
                return false;
            }
        }

        return true;
    }
    //============================================================================


    bool SipiImage::readOriginal(const std::string &filepath, const std::shared_ptr<SipiRegion>& region,
                                 const std::shared_ptr<SipiSize>& size, const std::string &origname, shttps::HashType htype) {
        read(filepath, region, size, false);

        if (!emdata.is_set()) {
            shttps::Hash internal_hash(htype);
            internal_hash.add_data(pixels, nx * ny * nc * bps / 8);
            std::string checksum = internal_hash.hash();
            std::string mimetype = shttps::Parsing::getFileMimetype(filepath).first;
            SipiEssentials emdata2(origname, mimetype, shttps::HashType::sha256, checksum);
            essential_metadata(emdata2);
        } else {
            shttps::Hash internal_hash(emdata.hash_type());
            internal_hash.add_data(pixels, nx * ny * nc * bps / 8);
            std::string checksum = internal_hash.hash();
            if (checksum != emdata.data_chksum()) {
                return false;
            }
        }

        return true;
    }
    //============================================================================

    SipiImgInfo SipiImage::getDim(const std::string &filepath) const {
        size_t pos = filepath.find_last_of('.');
        std::string fext = filepath.substr(pos + 1);
        std::string _fext;

        _fext.resize(fext.size());
        std::transform(fext.begin(), fext.end(), _fext.begin(), ::tolower);

        SipiImgInfo info;
        std::string mimetype = shttps::Parsing::getFileMimetype(filepath).first;
        info.internalmimetype = mimetype;

        if ((mimetype == "image/tiff") || (mimetype == "image/x-tiff")) {
            info = io[std::string("tif")]->getDim(filepath);
        } else if ((mimetype == "image/jpeg") || (mimetype == "image/pjpeg")) {
            info = io[std::string("jpg")]->getDim(filepath);
        } else if (mimetype == "image/png") {
            info = io[std::string("png")]->getDim(filepath);
        } else if ((mimetype == "image/jp2") || (mimetype == "image/jpx")) {
            info = io[std::string("jpx")]->getDim(filepath);
        } else {
            throw SipiImageError(__file__, __LINE__, "unknown mimetype: \"" + mimetype + "\"!");
        }

        if (info.success == SipiImgInfo::FAILURE) {
            for (auto const &iterator : io) {
                info = iterator.second->getDim(filepath);
                if (info.success != SipiImgInfo::FAILURE) break;
            }
        }

        if (info.success == SipiImgInfo::FAILURE) {
            throw SipiImageError(__file__, __LINE__,
                                 "Could not read file " + filepath);
        }
        return info;
    }
    //============================================================================


    void SipiImage::getDim(size_t &width, size_t &height) const {
        width = getNx();
        height = getNy();
    }
    //============================================================================

    void SipiImage::write(const std::string &ftype, const std::string &filepath, const SipiCompressionParams *params) {
        io[ftype]->write(this, filepath, params);
   }
    //============================================================================

    void SipiImage::convertYCC2RGB() {
        if (bps == 8) {
            byte *inbuf = pixels;
            byte *outbuf = new byte[(size_t) nc * (size_t) nx * (size_t) ny];

            for (size_t j = 0; j < ny; j++) {
                for (size_t i = 0; i < nx; i++) {
                    auto Y = (double) inbuf[nc * (j * nx + i) + 2];
                    auto Cb = (double) inbuf[nc * (j * nx + i) + 1];;
                    auto Cr = (double) inbuf[nc * (j * nx + i) + 0];

                    int r = (int) (Y + 1.40200 * (Cr - 0x80));
                    int g = (int) (Y - 0.34414 * (Cb - 0x80) - 0.71414 * (Cr - 0x80));
                    int b = (int) (Y + 1.77200 * (Cb - 0x80));

                    outbuf[nc * (j * nx + i) + 0] = std::max(0, std::min(255, r));
                    outbuf[nc * (j * nx + i) + 1] = std::max(0, std::min(255, g));
                    outbuf[nc * (j * nx + i) + 2] = std::max(0, std::min(255, b));

                    for (size_t k = 3; k < nc; k++) {
                        outbuf[nc * (j * nx + i) + k] = inbuf[nc * (j * nx + i) + k];
                    }
                }
            }

            pixels = outbuf;
            delete[] inbuf;
        } else if (bps == 16) {
            word *inbuf = (word *) pixels;
            size_t nnc = nc - 1;
            auto *outbuf = new unsigned short[nnc * nx * ny];

            for (size_t j = 0; j < ny; j++) {
                for (size_t i = 0; i < nx; i++) {
                    auto Y = (double) inbuf[nc * (j * nx + i) + 2];
                    auto Cb = (double) inbuf[nc * (j * nx + i) + 1];;
                    auto Cr = (double) inbuf[nc * (j * nx + i) + 0];

                    int r = (int) (Y + 1.40200 * (Cr - 0x80));
                    int g = (int) (Y - 0.34414 * (Cb - 0x80) - 0.71414 * (Cr - 0x80));
                    int b = (int) (Y + 1.77200 * (Cb - 0x80));

                    outbuf[nc * (j * nx + i) + 0] = std::max(0, std::min(65535, r));
                    outbuf[nc * (j * nx + i) + 1] = std::max(0, std::min(65535, g));
                    outbuf[nc * (j * nx + i) + 2] = std::max(0, std::min(65535, b));

                    for (size_t k = 3; k < nc; k++) {
                        outbuf[nc * (j * nx + i) + k] = inbuf[nc * (j * nx + i) + k];
                    }
                }
            }

            pixels = (byte *) outbuf;
            delete[] inbuf;
        } else {
            std::string msg = "Bits per sample is not supported for operation: " + std::to_string(bps);
            throw SipiImageError(__file__, __LINE__, msg);
        }
    }
    //============================================================================

    void SipiImage::convertToIcc(const SipiIcc &target_icc_p, int new_bps) {
        cmsSetLogErrorHandler(icc_error_logger);
        cmsUInt32Number in_formatter, out_formatter;

        if (icc == nullptr) {
            switch (nc) {
                case 1: {
                    icc = std::make_shared<SipiIcc>(icc_GRAY_D50); // assume gray value image with D50
                    break;
                }

                case 3: {
                    icc = std::make_shared<SipiIcc>(icc_sRGB); // assume sRGB
                    break;
                }

                case 4: {
                    icc = std::make_shared<SipiIcc>(icc_CYMK_standard); // assume CYMK
                    break;
                }

                default: {
                    throw SipiImageError(__file__, __LINE__,
                                         "Cannot assign ICC profile to image with nc=" + std::to_string(nc));
                }
            }
        }
        unsigned int nnc = cmsChannelsOf(cmsGetColorSpace(target_icc_p.getIccProfile()));

        if (!((new_bps == 8) || (new_bps == 16))) {
            throw SipiImageError(__file__, __LINE__, "Unsupported bits/sample (" + std::to_string(bps) + ")");
        }

        cmsHTRANSFORM hTransform;
        in_formatter = icc->iccFormatter(this);
        out_formatter = target_icc_p.iccFormatter(new_bps);

        hTransform = cmsCreateTransform(icc->getIccProfile(), in_formatter, target_icc_p.getIccProfile(), out_formatter,
                                        INTENT_PERCEPTUAL, 0);

        if (hTransform == nullptr) {
            throw SipiImageError(__file__, __LINE__, "Couldn't create color transform");
        }

        byte *inbuf = pixels;
        byte *outbuf = new byte[nx * ny * nnc * new_bps / 8];
        cmsDoTransform(hTransform, inbuf, outbuf, nx * ny);
        cmsDeleteTransform(hTransform);
        icc = std::make_shared<SipiIcc>(target_icc_p);
        pixels = outbuf;
        delete[] inbuf;
        nc = nnc;
        bps = new_bps;

        PredefinedProfiles targetPT = target_icc_p.getProfileType();
        switch (targetPT) {
            case icc_GRAY_D50: {
                photo = MINISBLACK;
                break;
            }

            case icc_RGB:
            case icc_sRGB:
            case icc_AdobeRGB: {
                photo = RGB;
                break;
            }

            case icc_CYMK_standard: {
                photo = SEPARATED;
                break;
            }

            case icc_LAB: {
                photo = CIELAB;
                break;
            }

            default: {
                // do nothing at the moment
            }
        }
    }

    /*==========================================================================*/


    void SipiImage::removeChan(unsigned int chan) {
        if ((nc == 1) || (chan >= nc)) {
            std::string msg = "Cannot remove component: nc=" + std::to_string(nc) + " chan=" + std::to_string(chan);
            throw SipiImageError(__file__, __LINE__, msg);
        }

        if (!es.empty()) {
            if (nc < 3) {
                es.clear(); // no more alpha channel
            } else if (nc > 3) { // it's probably an alpha channel
                if ((nc == 4) && (photo == SEPARATED)) {  // oh no – 4 channes, but CMYK
                    std::string msg =
                            "Cannot remove component: nc=" + std::to_string(nc) + " chan=" + std::to_string(chan);
                    throw SipiImageError(__file__, __LINE__, msg);
                } else {
                    es.erase(es.begin() + (chan - ((photo == SEPARATED) ? 4 : 3)));
                }
            } else {
                std::string msg = "Cannot remove component: nc=" + std::to_string(nc) + " chan=" + std::to_string(chan);
                throw SipiImageError(__file__, __LINE__, msg);
            }
        }

        if (bps == 8) {
            byte *inbuf = pixels;
            size_t nnc = nc - 1;
            byte *outbuf = new byte[(size_t) nnc * (size_t) nx * (size_t) ny];

            for (size_t j = 0; j < ny; j++) {
                for (size_t i = 0; i < nx; i++) {
                    for (size_t k = 0; k < nc; k++) {
                        if (k == chan) continue;
                        outbuf[nnc * (j * nx + i) + k] = inbuf[nc * (j * nx + i) + k];
                    }
                }
            }

            pixels = outbuf;
            delete[] inbuf;
        } else if (bps == 16) {
            word *inbuf = (word *) pixels;
            size_t nnc = nc - 1;
            auto *outbuf = new unsigned short[nnc * nx * ny];

            for (size_t j = 0; j < ny; j++) {
                for (size_t i = 0; i < nx; i++) {
                    for (size_t k = 0; k < nc; k++) {
                        if (k == chan) continue;
                        outbuf[nnc * (j * nx + i) + k] = inbuf[nc * (j * nx + i) + k];
                    }
                }
            }

            pixels = (byte *) outbuf;
            delete[] inbuf;
        } else {
            if (bps != 8) {
                std::string msg = "Bits per sample is not supported for operation: " + std::to_string(bps);
                throw SipiImageError(__file__, __LINE__, msg);
            }
        }

        nc--;
    }
    //============================================================================


    bool SipiImage::crop(int x, int y, size_t width, size_t height) {
        if (x < 0) {
            width += x;
            x = 0;
        } else if (x >= (long) nx) {
            return false;
        }

        if (y < 0) {
            height += y;
            y = 0;
        } else if (y >= (long) ny) {
            return false;
        }

        if (width == 0) {
            width = nx - x;
        } else if ((x + width) > nx) {
            width = nx - x;
        }

        if (height == 0) {
            height = ny - y;
        } else if ((y + height) > ny) {
            height = ny - y;
        }

        if ((x == 0) && (y == 0) && (width == nx) && (height == ny)) return true; //we do not have to crop!!

        if (bps == 8) {
            byte *inbuf = pixels;
            byte *outbuf = new byte[width * height * nc];

            for (size_t j = 0; j < height; j++) {
                for (size_t i = 0; i < width; i++) {
                    for (size_t k = 0; k < nc; k++) {
                        outbuf[nc * (j * width + i) + k] = inbuf[nc * ((j + y) * nx + (i + x)) + k];
                    }
                }
            }

            pixels = outbuf;
            delete[] inbuf;
        } else if (bps == 16) {
            word *inbuf = (word *) pixels;
            word *outbuf = new word[width * height * nc];

            for (size_t j = 0; j < height; j++) {
                for (size_t i = 0; i < width; i++) {
                    for (size_t k = 0; k < nc; k++) {
                        outbuf[nc * (j * width + i) + k] = inbuf[nc * ((j + y) * nx + (i + x)) + k];
                    }
                }
            }

            pixels = (byte *) outbuf;
            delete[] inbuf;
        } else {
            // clean up and throw exception
        }

        nx = width;
        ny = height;

        return true;
    }
    //============================================================================


    bool SipiImage::crop(const std::shared_ptr<SipiRegion> &region) {
        int x, y;
        size_t width, height;
        if (region->getType() == SipiRegion::FULL) return true; // we do not have to crop;
        region->crop_coords(nx, ny, x, y, width, height);

        if (bps == 8) {
            byte *inbuf = pixels;
            byte *outbuf = new byte[width * height * nc];

            for (size_t j = 0; j < height; j++) {
                for (size_t i = 0; i < width; i++) {
                    for (size_t k = 0; k < nc; k++) {
                        outbuf[nc * (j * width + i) + k] = inbuf[nc * ((j + y) * nx + (i + x)) + k];
                    }
                }
            }

            pixels = outbuf;
            delete[] inbuf;
        } else if (bps == 16) {
            word *inbuf = (word *) pixels;
            word *outbuf = new word[width * height * nc];

            for (size_t j = 0; j < height; j++) {
                for (size_t i = 0; i < width; i++) {
                    for (size_t k = 0; k < nc; k++) {
                        outbuf[nc * (j * width + i) + k] = inbuf[nc * ((j + y) * nx + (i + x)) + k];
                    }
                }
            }

            pixels = (byte *) outbuf;
            delete[] inbuf;
        } else {
            // clean up and throw exception
        }

        nx = width;
        ny = height;
        return true;
    }
    //============================================================================


    /****************************************************************************/
#define POSITION(x, y, c, n) ((n)*((y)*nx + (x)) + c)

    byte SipiImage::bilinn(byte buf[], const int nx, const double x, const double y, const int c, const int n) {
        const auto ix = static_cast<int>(x);
        const auto iy = static_cast<int>(y);
        const auto rx = x - static_cast<double>(ix);
        const auto ry = y - static_cast<double>(iy);

        constexpr double Threshold = 1.0e-2;

        if ((rx < Threshold) && (ry < Threshold)) {
            return (buf[POSITION(ix, iy, c, n)]);
        }

        if (rx < Threshold) {
            return ((byte) lround(((double) buf[POSITION(ix, iy, c, n)] * (1 - rx - ry + rx * ry) +
                             (double) buf[POSITION(ix, (iy + 1), c, n)] * (ry - rx * ry))));
        }

        if (ry < Threshold) {
            return ((byte) lround(((double) buf[POSITION(ix, iy, c, n)] * (1 - rx - ry + rx * ry) +
                             (double) buf[POSITION((ix + 1), iy, c, n)] * (rx - rx * ry))));
        }

        return ((byte) lround(((double) buf[POSITION(ix, iy, c, n)] * (1 - rx - ry + rx * ry) +
                             (double) buf[POSITION((ix + 1), iy, c, n)] * (rx - rx * ry) +
                             (double) buf[POSITION(ix, (iy + 1), c, n)] * (ry - rx * ry) +
                             (double) buf[POSITION((ix + 1), (iy + 1), c, n)] * rx * ry)));

    }

    /*==========================================================================*/

    word SipiImage::bilinn(word buf[], const int nx, const double x, const double y, const int c, const int n) {
        const auto ix = static_cast<int>(x);
        const auto iy = static_cast<int>(y);
        const auto rx = x - static_cast<double>(ix);
        const auto ry = y - static_cast<double>(iy);

        constexpr double Threshold = 1.0e-2;

        if ((rx < Threshold) && (ry < Threshold)) {
            return (buf[POSITION(ix, iy, c, n)]);
        }

        if (rx < Threshold) {
            return ((word) lround(((double) buf[POSITION(ix, iy, c, n)] * (1 - rx - ry + rx * ry) +
                             (double) buf[POSITION(ix, (iy + 1), c, n)] * (ry - rx * ry))));
        }

        if (ry < Threshold) {
            return ((word) lround(((double) buf[POSITION(ix, iy, c, n)] * (1 - rx - ry + rx * ry) +
                             (double) buf[POSITION((ix + 1), iy, c, n)] * (rx - rx * ry))));
        }

        return ((word) lround(((double) buf[POSITION(ix, iy, c, n)] * (1 - rx - ry + rx * ry) +
                             (double) buf[POSITION((ix + 1), iy, c, n)] * (rx - rx * ry) +
                             (double) buf[POSITION(ix, (iy + 1), c, n)] * (ry - rx * ry) +
                             (double) buf[POSITION((ix + 1), (iy + 1), c, n)] * rx * ry)));
    }
    /*==========================================================================*/

#undef POSITION

    bool SipiImage::scaleFast(size_t nnx, size_t nny) {
        auto xlut = shttps::make_unique<size_t[]>(nnx);
        auto ylut = shttps::make_unique<size_t[]>(nny);

        for (size_t i = 0; i < nnx; i++) {
            xlut[i] = (size_t) lround(i * (nx - 1) / (nnx - 1));
        }
        for (size_t i = 0; i < nny; i++) {
            ylut[i] = (size_t) lround(i * (ny - 1) / (nny - 1 ));
        }

        if (bps == 8) {
            byte *inbuf = pixels;
            byte *outbuf = new byte[nnx * nny * nc];
            for (size_t y = 0; y < nny; y++) {
                for (size_t x = 0; x < nnx; x++) {
                    for (size_t k = 0; k < nc; k++) {
                        outbuf[nc * (y * nnx + x) + k] = inbuf[nc * (ylut[y] * nx + xlut[x]) + k];
                    }
                }
            }
            pixels = outbuf;
            delete[] inbuf;
        } else if (bps == 16) {
            word *inbuf = (word *) pixels;
            word *outbuf = new word[nnx * nny * nc];
            for (size_t y = 0; y < nny; y++) {
                for (size_t x = 0; x < nnx; x++) {
                    for (size_t k = 0; k < nc; k++) {
                        outbuf[nc * (y * nnx + x) + k] = inbuf[nc * (ylut[y] * nx + xlut[x]) + k];
                    }
                }
            }
            pixels = (byte *) outbuf;
            delete[] inbuf;

        } else {
            return false;

        }

        nx = nnx;
        ny = nny;
        return true;
    }
    /*==========================================================================*/


    bool SipiImage::scaleMedium(size_t nnx, size_t nny) {
        auto xlut = shttps::make_unique<double[]>(nnx);
        auto ylut = shttps::make_unique<double[]>(nny);

        for (size_t i = 0; i < nnx; i++) {
            xlut[i] = (double) (i * (nx - 1)) / (double) (nnx - 1);
        }
        for (size_t j = 0; j < nny; j++) {
            ylut[j] = (double) (j * (ny - 1)) / (double) (nny - 1);
        }

        if (bps == 8) {
            byte *inbuf = pixels;
            byte *outbuf = new byte[nnx * nny * nc];
            double rx, ry;

            for (size_t j = 0; j < nny; j++) {
                ry = ylut[j];
                for (size_t i = 0; i < nnx; i++) {
                    rx = xlut[i];
                    for (size_t k = 0; k < nc; k++) {
                        outbuf[nc * (j * nnx + i) + k] = bilinn(inbuf, nx, rx, ry, k, nc);
                    }
                }
            }

            pixels = outbuf;
            delete[] inbuf;
        } else if (bps == 16) {
            word *inbuf = (word *) pixels;
            word *outbuf = new word[nnx * nny * nc];
            double rx, ry;

            for (size_t j = 0; j < nny; j++) {
                ry = ylut[j];
                for (size_t i = 0; i < nnx; i++) {
                    rx = xlut[i];
                    for (size_t k = 0; k < nc; k++) {
                        outbuf[nc * (j * nnx + i) + k] = bilinn(inbuf, nx, rx, ry, k, nc);
                    }
                }
            }

            pixels = (byte *) outbuf;
            delete[] inbuf;
        } else {
            return false;
        }

        nx = nnx;
        ny = nny;
        return true;
    }
    /*==========================================================================*/


    bool SipiImage::scale(size_t nnx, size_t nny) {
        size_t iix = 1, iiy = 1;
        size_t nnnx, nnny;

        //
        // if the scaling is less than 1 (that is, the image gets smaller), we first
        // expand it to a integer multiple of the desired size, and then we just
        // avarage the number of pixels. This is the "proper" way of downscale an
        // image...
        //
        if (nnx < nx) {
            while (nnx * iix < nx) iix++;
            nnnx = nnx * iix;
        } else {
            nnnx = nnx;
        }

        if (nny < ny) {
            while (nny * iiy < ny) iiy++;
            nnny = nny * iiy;
        } else {
            nnny = nny;
        }

        auto xlut = shttps::make_unique<double[]>(nnnx);
        auto ylut = shttps::make_unique<double[]>(nnny);

        for (size_t i = 0; i < nnnx; i++) {
            xlut[i] = (double) (i * (nx - 1)) / (double) (nnnx - 1);
        }
        for (size_t j = 0; j < nnny; j++) {
            ylut[j] = (double) (j * (ny - 1)) / (double) (nnny - 1);
        }

        if (bps == 8) {
            byte *inbuf = pixels;
            byte *outbuf = new byte[nnnx * nnny * nc];
            double rx, ry;

            for (size_t j = 0; j < nnny; j++) {
                ry = ylut[j];
                for (size_t i = 0; i < nnnx; i++) {
                    rx = xlut[i];
                    for (size_t k = 0; k < nc; k++) {
                        outbuf[nc * (j * nnnx + i) + k] = bilinn(inbuf, nx, rx, ry, k, nc);
                    }
                }
            }

            pixels = outbuf;
            delete[] inbuf;
        } else if (bps == 16) {
            word *inbuf = (word *) pixels;
            word *outbuf = new word[nnnx * nnny * nc];
            double rx, ry;

            for (size_t j = 0; j < nnny; j++) {
                ry = ylut[j];
                for (size_t i = 0; i < nnnx; i++) {
                    rx = xlut[i];
                    for (size_t k = 0; k < nc; k++) {
                        outbuf[nc * (j * nnnx + i) + k] = bilinn(inbuf, nx, rx, ry, k, nc);
                    }
                }
            }

            pixels = (byte *) outbuf;
            delete[] inbuf;
        } else {
            return false;
            // clean up and throw exception
        }

        //
        // now we have to check if we have to average the pixels
        //
        if ((iix > 1) || (iiy > 1)) {
            if (bps == 8) {
                byte *inbuf = pixels;
                byte *outbuf = new byte[nnx * nny * nc];
                for (size_t j = 0; j < nny; j++) {
                    for (size_t i = 0; i < nnx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            unsigned int accu = 0;

                            for (size_t jj = 0; jj < iiy; jj++) {
                                for (size_t ii = 0; ii < iix; ii++) {
                                    accu += inbuf[nc * ((iiy * j + jj) * nnnx + (iix * i + ii)) + k];
                                }
                            }

                            outbuf[nc * (j * nnx + i) + k] = accu / (iix * iiy);
                        }
                    }
                }
                pixels = outbuf;
                delete[] inbuf;
            } else if (bps == 16) {
                word *inbuf = (word *) pixels;
                word *outbuf = new word[nnx * nny * nc];

                for (size_t j = 0; j < nny; j++) {
                    for (size_t i = 0; i < nnx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            unsigned int accu = 0;

                            for (size_t jj = 0; jj < iiy; jj++) {
                                for (size_t ii = 0; ii < iix; ii++) {
                                    accu += inbuf[nc * ((iiy * j + jj) * nnnx + (iix * i + ii)) + k];
                                }
                            }

                            outbuf[nc * (j * nnx + i) + k] = accu / (iix * iiy);
                        }
                    }
                }

                pixels = (byte *) outbuf;
                delete[] inbuf;
            }
        }

        nx = nnx;
        ny = nny;
        return true;
    }
    //============================================================================


    bool SipiImage::rotate(float angle, bool mirror) {
        if (mirror) {
            if (bps == 8) {
                byte *inbuf = (byte *) pixels;
                byte *outbuf = new byte[nx * ny * nc];
                for (size_t j = 0; j < ny; j++) {
                    for (size_t i = 0; i < nx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            outbuf[nc * (j * nx + i) + k] = inbuf[nc * (j * nx + (nx - i - 1)) + k];
                        }
                    }
                }

                pixels = outbuf;
                delete[] inbuf;
            } else if (bps == 16) {
                word *inbuf = (word *) pixels;
                word *outbuf = new word[nx * ny * nc];

                for (size_t j = 0; j < ny; j++) {
                    for (size_t i = 0; i < nx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            outbuf[nc * (j * nx + i) + k] = inbuf[nc * (j * nx + (nx - i - 1)) + k];
                        }
                    }
                }

                pixels = (byte *) outbuf;
                delete[] inbuf;
            } else {
                return false;
                // clean up and throw exception
            }
        }

        while (angle < 0.) angle += 360.;
        while (angle >= 360.) angle -= 360.;

        if (angle == 0.) {
            return true;
        }

        if (angle == 90.) {
            //
            // abcdef     mga
            // ghijkl ==> nhb
            // mnopqr     oic
            //            pjd
            //            qke
            //            rlf
            //
            size_t nnx = ny;
            size_t nny = nx;

            if (bps == 8) {
                byte *inbuf = (byte *) pixels;
                byte *outbuf = new byte[nx * ny * nc];

                for (size_t j = 0; j < nny; j++) {
                    for (size_t i = 0; i < nnx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            outbuf[nc * (j * nnx + i) + k] = inbuf[nc * ((ny - i - 1) * nx + j) + k];
                        }
                    }
                }

                pixels = outbuf;
                delete[] inbuf;
            } else if (bps == 16) {
                word *inbuf = (word *) pixels;
                word *outbuf = new word[nx * ny * nc];

                for (size_t j = 0; j < nny; j++) {
                    for (size_t i = 0; i < nnx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            outbuf[nc * (j * nnx + i) + k] = inbuf[nc * ((ny - i - 1) * nx + j) + k];
                        }
                    }
                }

                pixels = (byte *) outbuf;
                delete[] inbuf;
            }

            nx = nnx;
            ny = nny;
        } else if (angle == 180.) {
            //
            // abcdef     rqponm
            // ghijkl ==> lkjihg
            // mnopqr     fedcba
            //
            size_t nnx = nx;
            size_t nny = ny;
            if (bps == 8) {
                byte *inbuf = (byte *) pixels;
                byte *outbuf = new byte[nx * ny * nc];

                for (size_t j = 0; j < nny; j++) {
                    for (size_t i = 0; i < nnx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            outbuf[nc * (j * nnx + i) + k] = inbuf[nc * ((ny - j - 1) * nx + (nx - i - 1)) + k];
                        }
                    }
                }

                pixels = outbuf;
                delete[] inbuf;
            } else if (bps == 16) {
                word *inbuf = (word *) pixels;
                word *outbuf = new word[nx * ny * nc];

                for (size_t j = 0; j < nny; j++) {
                    for (size_t i = 0; i < nnx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            outbuf[nc * (j * nnx + i) + k] = inbuf[nc * ((ny - j - 1) * nx + (nx - i - 1)) + k];
                        }
                    }
                }

                pixels = (byte *) outbuf;
                delete[] inbuf;
            }
            nx = nnx;
            ny = nny;
        } else if (angle == 270.) {
            //
            // abcdef     flr
            // ghijkl ==> ekq
            // mnopqr     djp
            //            cio
            //            bhn
            //            agm
            //
            size_t nnx = ny;
            size_t nny = nx;

            if (bps == 8) {
                byte *inbuf = (byte *) pixels;
                byte *outbuf = new byte[nx * ny * nc];
                for (size_t j = 0; j < nny; j++) {
                    for (size_t i = 0; i < nnx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            outbuf[nc * (j * nnx + i) + k] = inbuf[nc * (i * nx + (nx - j - 1)) + k];
                        }
                    }
                }

                pixels = outbuf;
                delete[] inbuf;
            } else if (bps == 16) {
                word *inbuf = (word *) pixels;
                word *outbuf = new word[nx * ny * nc];
                for (size_t j = 0; j < nny; j++) {
                    for (size_t i = 0; i < nnx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            outbuf[nc * (j * nnx + i) + k] = inbuf[nc * (i * nx + (nx - j - 1)) + k];
                        }
                    }
                }
                pixels = (byte *) outbuf;
                delete[] inbuf;
            }

            nx = nnx;
            ny = nny;
        } else { // all other angles
            double phi = M_PI * angle / 180.0;
            double ptx = static_cast<double>(nx) / 2. - .5;
            double pty = static_cast<double>(ny) / 2. - .5;

            double si = sin(-phi);
            double co = cos(-phi);

            size_t nnx;
            size_t nny;

            if ((angle > 0.) && (angle < 90.)) {
                nnx = floor((double) nx * cos(phi) + (double) ny * sin(phi) + .5);
                nny = floor((double) nx * sin(phi) + (double) ny * cos(phi) + .5);
            } else if ((angle > 90.) && (angle < 180.)) {
                nnx = floor(-((double) nx) * cos(phi) + (double) ny * sin(phi) + .5);
                nny = floor((double) nx * sin(phi) - (double) ny * cos(phi) + .5);
            } else if ((angle > 180.) && (angle < 270.)) {
                nnx = floor(-((double) nx) * cos(phi) - (double) ny * sin(phi) + .5);
                nny = floor(-((double) nx) * sin(phi) - (double) ny * cos(phi) + .5);
            } else {
                nnx = floor((double) nx * cos(phi) - (double) ny * sin(phi) + .5);
                nny = floor(-((double) nx) * sin(phi) + (double) ny * cos(phi) + .5);
            }

            double pptx = ptx * (double) nnx / (double) nx;
            double ppty = pty * (double) nny / (double) ny;

            if (bps == 8) {
                byte *inbuf = pixels;
                byte *outbuf = new byte[nnx * nny * nc];
                byte bg = 0;

                for (size_t j = 0; j < nny; j++) {
                    for (size_t i = 0; i < nnx; i++) {
                        double rx = ((double) i - pptx) * co - ((double) j - ppty) * si + ptx;
                        double ry = ((double) i - pptx) * si + ((double) j - ppty) * co + pty;

                        if ((rx < 0.0) || (rx >= (double) (nx - 1)) || (ry < 0.0) || (ry >= (double) (ny - 1))) {
                            for (size_t k = 0; k < nc; k++) {
                                outbuf[nc * (j * nnx + i) + k] = bg;
                            }
                        } else {
                            for (size_t k = 0; k < nc; k++) {
                                outbuf[nc * (j * nnx + i) + k] = bilinn(inbuf, nx, rx, ry, k, nc);
                            }
                        }
                    }
                }

                pixels = outbuf;
                delete[] inbuf;
            } else if (bps == 16) {
                word *inbuf = (word *) pixels;
                word *outbuf = new word[nnx * nny * nc];
                word bg = 0;

                for (size_t j = 0; j < nny; j++) {
                    for (size_t i = 0; i < nnx; i++) {
                        double rx = ((double) i - pptx) * co - ((double) j - ppty) * si + ptx;
                        double ry = ((double) i - pptx) * si + ((double) j - ppty) * co + pty;

                        if ((rx < 0.0) || (rx >= (double) (nx - 1)) || (ry < 0.0) || (ry >= (double) (ny - 1))) {
                            for (size_t k = 0; k < nc; k++) {
                                outbuf[nc * (j * nnx + i) + k] = bg;
                            }
                        } else {
                            for (size_t k = 0; k < nc; k++) {
                                outbuf[nc * (j * nnx + i) + k] = bilinn(inbuf, nx, rx, ry, k, nc);
                            }
                        }
                    }
                }

                pixels = (byte *) outbuf;
                delete[] inbuf;
            }
            nx = nnx;
            ny = nny;
        }
        return true;
    }
    //============================================================================


    bool SipiImage::set_topleft() {
        switch (orientation) {
            case TOPLEFT: // 1
                return true;
            case TOPRIGHT: // 2
                rotate(0., true);
                break;
            case BOTRIGHT: // 3
                rotate(180., false);
                break;
            case BOTLEFT: // 4
                rotate(180., true);
                break;
            case LEFTTOP: // 5
                rotate(270., true);
                break;
            case RIGHTTOP: // 6
                rotate(90., false);
                break;
            case RIGHTBOT: // 7
                rotate(90., true);
                break;
            case LEFTBOT: // 8
                rotate(270., false);
                break;
            default:
                ; // nothing to do...
        }
        orientation = TOPLEFT;
        if (exif != nullptr) {
            exif->addKeyVal("Exif.Image.Orientation", static_cast<unsigned short>(TOPLEFT));
        }
        return true;
    }
    //============================================================================

    bool SipiImage::to8bps() {
        // little-endian architecture assumed
        //
        // we just use the shift-right operater (>> 8) to devide the values by 256 (2^8)!
        // This is the most efficient and fastest way
        //
        if (bps == 16) {
            //icc = NULL;

            word *inbuf = (word *) pixels;
            //byte *outbuf = new(std::nothrow) Sipi::byte[nc*nx*ny];
            byte *outbuf = new(std::nothrow) byte[nc * nx * ny];
            if (outbuf == nullptr) return false;
            for (size_t j = 0; j < ny; j++) {
                for (size_t i = 0; i < nx; i++) {
                    for (size_t k = 0; k < nc; k++) {
                        // divide pixel values by 256 using ">> 8"
                        outbuf[nc * (j * nx + i) + k] = (inbuf[nc * (j * nx + i) + k] >> 8);
                    }
                }
            }

            delete[] pixels;
            pixels = outbuf;
            bps = 8;
        }
        return true;
    }
    //============================================================================


    bool SipiImage::toBitonal() {
        if ((photo != MINISBLACK) && (photo != MINISWHITE)) {
            convertToIcc(SipiIcc(icc_GRAY_D50), 8);
        }

        bool doit = false; // will be set true if we find a value not equal 0 or 255

        for (size_t i = 0; i < nx * ny; i++) {
            if (!doit && (pixels[i] != 0) && (pixels[i] != 255)) doit = true;
        }

        if (!doit) return true; // we have to do nothing, it's already bitonal

        // must be signed!! Error propagation my result in values < 0 or > 255
        auto *outbuf = new(std::nothrow) short[nx * ny];

        if (outbuf == nullptr) return false; // TODO: throw an error with a reasonable error message

        for (size_t i = 0; i < nx * ny; i++) {
            outbuf[i] = pixels[i];  // copy buffer
        }

        for (size_t y = 0; y < ny; y++) {
            for (size_t x = 0; x < nx; x++) {
                short oldpixel = outbuf[y * nx + x];
                outbuf[y * nx + x] = (oldpixel > 127) ? 255 : 0;
                int properr = (oldpixel - outbuf[y * nx + x]);
                if (x < (nx - 1)) outbuf[y * nx + (x + 1)] += (7 * properr) >> 4;
                if ((x > 0) && (y < (ny - 1))) outbuf[(y + 1) * nx + (x - 1)] += (3 * properr) >> 4;
                if (y < (ny - 1)) outbuf[(y + 1) * nx + x] += (5 * properr) >> 4;
                if ((x < (nx - 1)) && (y < (ny - 1))) outbuf[(y + 1) * nx + (x + 1)] += properr >> 4;
            }
        }

        for (size_t i = 0; i < nx * ny; i++) pixels[i] = outbuf[i];
        delete[] outbuf;
        return true;
    }
    //============================================================================


    void SipiImage::add_watermark(const std::string &wmfilename) {
        int wm_nx, wm_ny, wm_nc;
        byte *wmbuf = read_watermark(wmfilename, wm_nx, wm_ny, wm_nc);
        if (wmbuf == nullptr) {
            throw SipiImageError(__file__, __LINE__, "Cannot read watermark file " + wmfilename);
        }

        auto xlut = shttps::make_unique<double[]>(nx);
        auto ylut = shttps::make_unique<double[]>(ny);

        //float *xlut = new float[nx];
        //float *ylut = new float[ny];

        for (size_t i = 0; i < nx; i++) {
            xlut[i] = (double) (wm_nx * i) / (double) nx;
        }

        for (size_t j = 0; j < ny; j++) {
            ylut[j] = (double) (wm_ny * j) / (double) ny;
        }

        if (bps == 8) {
            auto *buf = pixels;

            for (size_t j = 0; j < ny; j++) {
                for (size_t i = 0; i < nx; i++) {
                    byte val = bilinn(wmbuf, wm_nx, xlut[i], ylut[j], 0, wm_nc);
                    for (size_t k = 0; k < nc; k++) {
                        double nval = (buf[nc * (j * nx + i) + k] / 255.) * (1.0 + val / 2550.0) + val / 2550.0;
                        buf[nc * (j * nx + i) + k] = (nval > 1.0) ? 255 : (unsigned char) floorl(nval * 255. + .5);
                    }
                }
            }
        } else if (bps == 16) {
            auto *buf = reinterpret_cast<word *>(pixels);

            for (size_t j = 0; j < ny; j++) {
                for (size_t i = 0; i < nx; i++) {
                    for (size_t k = 0; k < nc; k++) {
                        byte val = bilinn(wmbuf, wm_nx, xlut[i], ylut[j], 0, wm_nc);
                        double nval =
                                (buf[nc * (j * nx + i) + k] / 65535.0) * (1.0 + val / 655350.0) + val / 352500.;
                        buf[nc * (j * nx + i) + k] = (nval > 1.0) ? (word) 65535 : (word) floorl(nval * 65535. + .5);
                    }
                }
            }
        }

        delete[] wmbuf;
   }

    /*==========================================================================*/


    SipiImage &SipiImage::operator-=(const SipiImage &rhs) {
        SipiImage *new_rhs = nullptr;

        if ((nc != rhs.nc) || (bps != rhs.bps) || (photo != rhs.photo)) {
            std::stringstream ss;
            ss << "Image op: images not compatible" << std::endl;
            ss << "Image 1:  nc: " << nc << " bps: " << bps << " photo: " << shttps::as_integer(photo) << std::endl;
            ss << "Image 2:  nc: " << rhs.nc << " bps: " << rhs.bps << " photo: " << shttps::as_integer(rhs.photo)
               << std::endl;
            throw SipiImageError(__file__, __LINE__, ss.str());
        }

        if ((nx != rhs.nx) || (ny != rhs.ny)) {
            new_rhs = new SipiImage(rhs);
            new_rhs->scale(nx, ny);
        }

        int *diffbuf = new int[nx * ny * nc];

        switch (bps) {
            case 8: {
                byte *ltmp = pixels;
                byte *rtmp = (new_rhs == nullptr) ? rhs.pixels : new_rhs->pixels;

                for (size_t j = 0; j < ny; j++) {
                    for (size_t i = 0; i < nx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            if (ltmp[nc * (j * nx + i) + k] != rtmp[nc * (j * nx + i) + k]) {
                                diffbuf[nc * (j * nx + i) + k] =
                                        ltmp[nc * (j * nx + i) + k] - rtmp[nc * (j * nx + i) + k];
                            }
                        }
                    }
                }

                break;
            }

            case 16: {
                word *ltmp = (word *) pixels;
                word *rtmp = (new_rhs == nullptr) ? (word *) rhs.pixels : (word *) new_rhs->pixels;

                for (size_t j = 0; j < ny; j++) {
                    for (size_t i = 0; i < nx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            if (ltmp[nc * (j * nx + i) + k] != rtmp[nc * (j * nx + i) + k]) {
                                diffbuf[nc * (j * nx + i) + k] =
                                        ltmp[nc * (j * nx + i) + k] - rtmp[nc * (j * nx + i) + k];
                            }
                        }
                    }
                }

                break;
            }

            default: {
                delete[] diffbuf;
                delete new_rhs;
                throw SipiImageError(__file__, __LINE__, "Bits per pixels not supported");
            }
        }

        int min = INT_MAX;
        int max = INT_MIN;

        for (size_t j = 0; j < ny; j++) {
            for (size_t i = 0; i < nx; i++) {
                for (size_t k = 0; k < nc; k++) {
                    if (diffbuf[nc * (j * nx + i) + k] > max) max = diffbuf[nc * (j * nx + i) + k];
                    if (diffbuf[nc * (j * nx + i) + k] < min) min = diffbuf[nc * (j * nx + i) + k];
                }
            }
        }
        int maxmax = abs(min) > abs(max) ? abs(min) : abs(max);

        switch (bps) {
            case 8: {
                byte *ltmp = pixels;

                for (size_t j = 0; j < ny; j++) {
                    for (size_t i = 0; i < nx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            ltmp[nc * (j * nx + i) + k] = (byte) ((diffbuf[nc * (j * nx + i) + k] + maxmax) *
                                                                  UCHAR_MAX / (2 * maxmax));
                        }
                    }
                }

                break;
            }

            case 16: {
                word *ltmp = (word *) pixels;

                for (size_t j = 0; j < ny; j++) {
                    for (size_t i = 0; i < nx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            ltmp[nc * (j * nx + i) + k] = (word) ((diffbuf[nc * (j * nx + i) + k] + maxmax) *
                                                                  USHRT_MAX / (2 * maxmax));
                        }
                    }
                }

                break;
            }

            default: {
                delete[] diffbuf;
                delete new_rhs;
                throw SipiImageError(__file__, __LINE__, "Bits per pixels not supported");
            }
        }

        delete new_rhs;

        delete[] diffbuf;
        return *this;
    }

    /*==========================================================================*/

    SipiImage &SipiImage::operator-(const SipiImage &rhs) {
        auto *lhs = new SipiImage(*this);
        *lhs -= rhs;
        return *lhs;
    }

    /*==========================================================================*/

    SipiImage &SipiImage::operator+=(const SipiImage &rhs) {
        SipiImage *new_rhs = nullptr;

        if ((nc != rhs.nc) || (bps != rhs.bps) || (photo != rhs.photo)) {
            std::stringstream ss;
            ss << "Image op: images not compatible" << std::endl;
            ss << "Image 1:  nc: " << nc << " bps: " << bps << " photo: " << shttps::as_integer(photo) << std::endl;
            ss << "Image 2:  nc: " << rhs.nc << " bps: " << rhs.bps << " photo: " << shttps::as_integer(rhs.photo)
               << std::endl;
            throw SipiImageError(__file__, __LINE__, ss.str());
        }

        if ((nx != rhs.nx) || (ny != rhs.ny)) {
            new_rhs = new SipiImage(rhs);
            new_rhs->scale(nx, ny);
        }

        int *diffbuf = new int[nx * ny * nc];

        switch (bps) {
            case 8: {
                byte *ltmp = pixels;
                byte *rtmp = (new_rhs == nullptr) ? rhs.pixels : new_rhs->pixels;

                for (size_t j = 0; j < ny; j++) {
                    for (size_t i = 0; i < nx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            if (ltmp[nc * (j * nx + i) + k] != rtmp[nc * (j * nx + i) + k]) {
                                diffbuf[nc * (j * nx + i) + k] =
                                        ltmp[nc * (j * nx + i) + k] + rtmp[nc * (j * nx + i) + k];
                            }
                        }
                    }
                }
                break;
            }

            case 16: {
                word *ltmp = (word *) pixels;
                word *rtmp = (new_rhs == nullptr) ? (word *) rhs.pixels : (word *) new_rhs->pixels;

                for (size_t j = 0; j < ny; j++) {
                    for (size_t i = 0; i < nx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            if (ltmp[nc * (j * nx + i) + k] != rtmp[nc * (j * nx + i) + k]) {
                                diffbuf[nc * (j * nx + i) + k] =
                                        ltmp[nc * (j * nx + i) + k] - rtmp[nc * (j * nx + i) + k];
                            }
                        }
                    }
                }

                break;
            }

            default: {
                delete[] diffbuf;
                delete new_rhs;
                throw SipiImageError(__file__, __LINE__, "Bits per pixels not supported");
            }
        }

        int max = INT_MIN;

        for (size_t j = 0; j < ny; j++) {
            for (size_t i = 0; i < nx; i++) {
                for (size_t k = 0; k < nc; k++) {
                    if (diffbuf[nc * (j * nx + i) + k] > max) max = diffbuf[nc * (j * nx + i) + k];
                }
            }
        }

        switch (bps) {
            case 8: {
                byte *ltmp = pixels;

                for (size_t j = 0; j < ny; j++) {
                    for (size_t i = 0; i < nx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            ltmp[nc * (j * nx + i) + k] = (byte) (diffbuf[nc * (j * nx + i) + k] * UCHAR_MAX / max);
                        }
                    }
                }

                break;
            }

            case 16: {
                word *ltmp = (word *) pixels;

                for (size_t j = 0; j < ny; j++) {
                    for (size_t i = 0; i < nx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            ltmp[nc * (j * nx + i) + k] = (word) (diffbuf[nc * (j * nx + i) + k] * USHRT_MAX / max);
                        }
                    }
                }

                break;
            }

            default: {
                delete[] diffbuf;
                delete new_rhs;
                throw SipiImageError(__file__, __LINE__, "Bits per pixels not supported");
            }
        }

        delete[] diffbuf;

        return *this;
    }

    /*==========================================================================*/

    SipiImage &SipiImage::operator+(const SipiImage &rhs) {
        auto *lhs = new SipiImage(*this);
        *lhs += rhs;
        return *lhs;
    }

    /*==========================================================================*/

    bool SipiImage::operator==(const SipiImage &rhs) {
        if ((nx != rhs.nx) || (ny != rhs.ny) || (nc != rhs.nc) || (bps != rhs.bps) || (photo != rhs.photo)) {
            return false;
        }

        long long n_differences = 0;

        switch (bps) {
            case 8: {
                byte *ltmp1 = pixels;
                byte *ltmp2 = rhs.pixels;
                for (size_t j = 0; j < ny; j++) {
                    for (size_t i = 0; i < nx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            if (ltmp1[nc * (j * nx + i) + k] != ltmp2[nc * (j * nx + i) + k]) {
                                n_differences++;
                            }
                        }
                    }
                }
                break;
            }
            case 16: {
                word *ltmp1 = (word *) pixels;
                word *ltmp2 = (word *) pixels;
                for (size_t j = 0; j < ny; j++) {
                    for (size_t i = 0; i < nx; i++) {
                        for (size_t k = 0; k < nc; k++) {
                            if (ltmp1[nc * (j * nx + i) + k] != ltmp2[nc * (j * nx + i) + k]) {
                                n_differences++;
                            }
                        }
                    }
                }
                break;
            }
        }

        return n_differences <= 0;
    }

    /*==========================================================================*/


    std::ostream &operator<<(std::ostream &outstr, const SipiImage &rhs) {
        outstr << std::endl << "SipiImage with the following parameters:" << std::endl;
        outstr << "nx    = " << std::to_string(rhs.nx) << std::endl;
        outstr << "ny    = " << std::to_string(rhs.ny) << std::endl;
        outstr << "nc    = " << std::to_string(rhs.nc) << std::endl;
        outstr << "es    = " << std::to_string(rhs.es.size()) << std::endl;
        outstr << "bps   = " << std::to_string(rhs.bps) << std::endl;
        outstr << "photo = " << std::to_string(rhs.photo) << std::endl;

        if (rhs.xmp) {
            outstr << "XMP-Metadata: " << std::endl << *(rhs.xmp) << std::endl;
        }

        if (rhs.iptc) {
            outstr << "IPTC-Metadata: " << std::endl << *(rhs.iptc) << std::endl;
        }

        if (rhs.exif) {
            outstr << "EXIF-Metadata: " << std::endl << *(rhs.exif) << std::endl;
        }

        if (rhs.icc) {
            outstr << "ICC-Metadata: " << std::endl << *(rhs.icc) << std::endl;
        }

        return outstr;
    }
    //============================================================================

}
