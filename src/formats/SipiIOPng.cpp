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
#include <cassert>
#include <cstdlib>
#include <arpa/inet.h>

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdio>
#include <cmath>
#include <syslog.h>

#include <cstring>

#include "SipiIOPng.h"


#include <png.h>
#include <zlib.h>


// bad hack in order to include definitions in png.h on debian systems
#if !defined(PNG_TEXT_SUPPORTED)
#  define PNG_TEXT_SUPPORTED 1
#endif
#if !defined(PNG_iTXt_SUPPORTED)
#  define PNG_iTXt_SUPPORTED 1
#endif

#define PNG_BYTES_TO_CHECK 4

static const char __file__[] = __FILE__;

namespace Sipi {

    static char lang_en[] = "en";
    static char exif_tag[] = "Raw profile type exif";
    static char iptc_tag[] = "Raw profile type iptc";
    static char xmp_tag[] = "XML:com.adobe.xmp";
    static char sipi_tag[] = "SIPI:io.sipi.essentials";

    //============== HELPER CLASS ==================
    class PngTextPtr {
    private:
        png_text *text_ptr;
        unsigned int num_text;
        unsigned int num_text_len;
    public:
        inline PngTextPtr(unsigned int len = 16) : num_text_len(len) {
            num_text = 0;
            text_ptr = new png_text[num_text_len];
        };

        inline ~PngTextPtr() { delete[] text_ptr; };

        inline unsigned int num() const { return num_text; };

        inline png_text *ptr() { return text_ptr; };

        png_text *next();

        void add_zTXt(char *key, char *data, unsigned int len);

        void add_iTXt(char *key, char *data, unsigned int len);
    };

    png_text *PngTextPtr::next() {
        if (num_text < num_text_len) {
            return &(text_ptr[num_text++]);
        } else {
            auto *tmpptr = new png_text[num_text_len + 16];
            for (unsigned int i = 0; i < num_text_len; i++) tmpptr[i] = text_ptr[i];
            delete[] text_ptr;
            text_ptr = tmpptr;
            return &(text_ptr[num_text++]);
        }
    }
    //=============================================

    void PngTextPtr::add_zTXt(char *key, char *data, unsigned int len) {
        png_text *tmp = this->next();
        tmp->compression = PNG_TEXT_COMPRESSION_zTXt;
        tmp->key = key;
        tmp->text = (char *) data;
        tmp->text_length = len;
        tmp->itxt_length = 0;
        tmp->lang = (char *) "";
        tmp->lang_key = (char *) "";
    }
    //=============================================

    void PngTextPtr::add_iTXt(char *key, char *data, unsigned int len) {
        png_text *tmp = this->next();
        tmp->compression = PNG_ITXT_COMPRESSION_zTXt;
        tmp->key = key;
        tmp->text = data;
        tmp->text_length = 0;
        tmp->itxt_length = len;
        tmp->lang = (char *) "";
        tmp->lang_key = (char *) "";
    }
    //=============================================

    void sipi_error_fn(png_structp png_ptr, png_const_charp error_msg) {
        std::cerr << "PNG ERROR: " << error_msg << std::endl;
        throw Sipi::SipiError(__file__, __LINE__, error_msg);
    }

    void sipi_warning_fn(png_structp png_ptr, png_const_charp warning_msg) {
        std::cerr << "PNG WARNING: " << warning_msg << std::endl;
    }

    bool SipiIOPng::read(SipiImage *img, const std::string &filepath, int pagenum, std::shared_ptr<SipiRegion> region,
                         std::shared_ptr<SipiSize> size, bool force_bps_8,
                         ScalingQuality scaling_quality)
    {
        FILE *infile;
        unsigned char header[PNG_BYTES_TO_CHECK];
        png_structp png_ptr;
        png_infop info_ptr;

        //
        // open the input file
        //
        if ((infile = fopen(filepath.c_str(), "rb")) == nullptr) {
            return FALSE;
        }

        //
        // check header if we really have a PNG file...
        //
        fread(header, 1, PNG_BYTES_TO_CHECK, infile);
        if (png_sig_cmp(header, 0, PNG_BYTES_TO_CHECK) != 0) {
            fclose(infile);
            return FALSE; // it's not a PNG file
        }

        if ((png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp) nullptr, sipi_error_fn,
                                              sipi_warning_fn)) == nullptr) {
            fclose(infile);
            throw SipiImageError(__file__, __LINE__, "Error reading PNG file \"" + filepath +
                                                     "\": Could not allocate mempry fpr png_structp !");
        }
        if ((info_ptr = png_create_info_struct(png_ptr)) == nullptr) {
            fclose(infile);
            throw SipiImageError(__file__, __LINE__, "Error reading PNG file \"" + filepath +
                                                     "\": Could not allocate mempry fpr png_infop !");
        }

        png_init_io(png_ptr, infile);
        png_set_sig_bytes(png_ptr, PNG_BYTES_TO_CHECK);
        png_read_info(png_ptr, info_ptr);

        png_uint_32 width, height;
        int color_type, bit_depth, interlace_type;
        png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
                     &interlace_type, NULL, NULL);
        img->nx = width;
        img->ny = height;
        img->orientation = TOPLEFT;

        png_set_packing(png_ptr);

        png_uint_32 res_x, res_y;
        int unit_type;
        if (png_get_pHYs(png_ptr, info_ptr, &res_x, &res_y, &unit_type)) {
            img->exif = std::make_shared<SipiExif>();
            float fres_x, fres_y;
            if (unit_type == PNG_RESOLUTION_METER) {
                fres_x = res_x / 39.37007874015748;
                fres_y = res_y / 39.37007874015748;
            }
            else {
                fres_x = res_x;
                fres_y = res_y;
            }
            img->exif->addKeyVal("Exif.Image.XResolution", fres_x);
            img->exif->addKeyVal("Exif.Image.YResolution", fres_y);
            img->exif->addKeyVal("Exif.Image.ResolutionUnit", 2); // DPI
        }

        switch (color_type) {
            case PNG_COLOR_TYPE_GRAY: { // implies nc = 1, (bit depths 1, 2, 4, 8, 16)
                png_set_expand_gray_1_2_4_to_8(png_ptr);
                img->photo = MINISBLACK;
                break;
            }
            case PNG_COLOR_TYPE_GRAY_ALPHA: { // implies nc = 2, (bit depths 8, 16)
                png_set_expand_gray_1_2_4_to_8(png_ptr);
                img->photo = MINISBLACK;
                img->es.push_back(ASSOCALPHA);
                break;
            }
            case PNG_COLOR_TYPE_PALETTE: { // might have an alpha channel – we check further below...
                png_set_palette_to_rgb(png_ptr);
                img->photo = RGB;
                break;
            }
            case PNG_COLOR_TYPE_RGB: { // implies nc = 3 (standard case :-), (bit_depths 8, 16)
                img->photo = RGB;
                break;
            }
            case PNG_COLOR_TYPE_RGBA: { // implies nc = 4, (bit_depths 8, 16)
                img->photo = RGB;
                img->es.push_back(ASSOCALPHA);
                break;
            }
        }
        if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS) != 0)
            png_set_tRNS_to_alpha(png_ptr);

        png_color_16 *image_background;
        if (png_get_bKGD(png_ptr, info_ptr, &image_background) != 0) {
            png_set_background(png_ptr, image_background,
                               PNG_BACKGROUND_GAMMA_FILE, 1, 1.0);
        }

        png_read_update_info(png_ptr, info_ptr);

        //
        // check for ICC profiles...
        //
        int srgb_intent;
        if (png_get_sRGB(png_ptr, info_ptr, &srgb_intent) != 0) {
            img->icc = std::make_shared<SipiIcc>(icc_sRGB);
        } else {
            png_charp name;
            int compression_type = PNG_COMPRESSION_TYPE_BASE;
            png_bytep profile;
            png_uint_32 proflen;
            if (png_get_iCCP(png_ptr, info_ptr, &name, &compression_type, &profile, &proflen) != 0) {
                img->icc = std::make_shared<SipiIcc>((unsigned char *) profile, (int) proflen);
            }
        }
        png_text *png_texts;
        int num_comments = png_get_text(png_ptr, info_ptr, &png_texts, nullptr);

        for (int i = 0; i < num_comments; i++) {
            if (strcmp(png_texts[i].key, xmp_tag) == 0) {
                img->xmp = std::make_shared<SipiXmp>((char *) png_texts[i].text, (int) png_texts[i].text_length);
            } else if (strcmp(png_texts[i].key, exif_tag) == 0) {
                try {
                    img->exif = std::make_shared<SipiExif>((unsigned char *) png_texts[i].text,
                                                           (unsigned int) png_texts[i].text_length);
                }
                catch (SipiError &err) {
                    //TODO: better error handling – now we nothing at all
                }
            } else if (strcmp(png_texts[i].key, iptc_tag) == 0) {
                img->iptc = std::make_shared<SipiIptc>((unsigned char *) png_texts[i].text,
                                                       (unsigned int) png_texts[i].text_length);
            } else if (strcmp(png_texts[i].key, sipi_tag) == 0) {
                SipiEssentials se(png_texts[i].text);
                img->essential_metadata(se);
            } else {
                fprintf(stderr, "PNG-COMMENT: key=\"%s\" text=\"%s\"\n", png_texts[i].key, png_texts[i].text);
            }
        }

        size_t sll = png_get_rowbytes(png_ptr, info_ptr);
        auto *buffer = new uint8[height * sll];

        auto *row_pointers = new png_bytep[height];
        for (size_t i = 0; i < img->ny; i++) {
            row_pointers[i] = (buffer + i * sll);
        }

        png_read_image(png_ptr, row_pointers);
        png_read_end(png_ptr, info_ptr);
        img->bps = png_get_bit_depth(png_ptr, info_ptr);
        img->nc = png_get_channels(png_ptr, info_ptr);
        if (color_type == PNG_COLOR_TYPE_PALETTE && img->nc == 4) {
            img->es.push_back(ASSOCALPHA);
        }

        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

        if (img->bps == 16) {
            auto *tmp = (unsigned short *) buffer;
            for (int i = 0; i < img->nx*img->ny*img->nc; i++) {
                tmp[i] = ntohs(tmp[i]);
            }
        }
        img->pixels = buffer;

        delete[] row_pointers;
        fclose(infile);

        if (region != nullptr) { //we just use the image.crop method
            (void) img->crop(region);
        }

        //
        // resize/Scale the image if necessary
        //
        if (size != nullptr) {
            size_t nnx, nny;
            int reduce = -1;
            bool redonly;
            SipiSize::SizeType rtype = size->get_size(img->nx, img->ny, nnx, nny, reduce, redonly);
            if (rtype != SipiSize::FULL) {
                switch (scaling_quality.png) {
                    case HIGH: img->scale(nnx, nny);
                        break;
                    case MEDIUM: img->scaleMedium(nnx, nny);
                        break;
                    case LOW: img->scaleFast(nnx, nny);
                }
            }
        }

        if (force_bps_8) {
            if (!img->to8bps()) {
                throw SipiImageError(__file__, __LINE__, "Cannot convert to 8 Bits(sample");
            }
        }
        return true;
    };

    /*==========================================================================*/


    SipiImgInfo SipiIOPng::getDim(const std::string &filepath, int pagenum) {
        FILE *infile;
        SipiImgInfo info;
        unsigned char header[8];

        //
        // open the input file
        //
        if ((infile = fopen(filepath.c_str(), "rb")) == nullptr) {
            info.success = SipiImgInfo::FAILURE;
            return info;
        }
        fread(header, 1, 8, infile);
        if (png_sig_cmp(header, 0, 8) != 0) {
            fclose(infile);
            info.success = SipiImgInfo::FAILURE;
            return info;        }

        png_structp png_ptr;
        png_infop info_ptr;

        if ((png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp) nullptr, (png_error_ptr) sipi_error_fn,
                                              (png_error_ptr) sipi_warning_fn)) == nullptr) {
            fclose(infile);
            throw SipiImageError(__file__, __LINE__, "Error reading PNG file \"" + filepath +
                                                     "\": Could not allocate mempry fpr png_structp !");
        }
        if ((info_ptr = png_create_info_struct(png_ptr)) == nullptr) {
            fclose(infile);
            throw SipiImageError(__file__, __LINE__, "Error reading PNG file \"" + filepath +
                                                     "\": Could not allocate mempry fpr png_infop !");
        }

        png_init_io(png_ptr, infile);
        png_set_sig_bytes(png_ptr, 8);

        info.width = png_get_image_width(png_ptr, info_ptr);
        info.height = png_get_image_height(png_ptr, info_ptr);
        info.orientation = TOPLEFT;
        info.success = SipiImgInfo::DIMS;

        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

        return info;
    }
    /*==========================================================================*/


    void create_text_chunk(PngTextPtr *png_textptr, char *key, char *str, unsigned int len) {
        png_text *chunk = png_textptr->next();
        chunk->compression = PNG_TEXT_COMPRESSION_NONE;
        chunk->key = key;
        chunk->text = str;
        chunk->text_length = len;
        chunk->itxt_length = 0;
        chunk->lang = lang_en;
        chunk->lang_key = nullptr;
    }

    /*==========================================================================*/

    static void conn_write_data(png_structp png_ptr, png_bytep data, png_size_t length) {
        shttps::Connection *conn = (shttps::Connection *) png_get_io_ptr(png_ptr);

        try {
            conn->sendAndFlush(data, length);
        } catch (int i) {
            // TODO: do nothing ??
        }

    }

    /*==========================================================================*/

    static void conn_flush_data(png_structp png_ptr) {
        shttps::Connection *conn = (shttps::Connection *) png_get_io_ptr(png_ptr);

        try {
            conn->flush();
        } catch (int i) {
            // TODO: do nothing ??
        }
    }

    /*==========================================================================*/

    void SipiIOPng::write(SipiImage *img, const std::string &filepath, const SipiCompressionParams *params) {
        FILE *outfile = nullptr;
        png_structp png_ptr;

        if (!(png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, sipi_error_fn, sipi_warning_fn))) {
            throw SipiImageError(__file__, __LINE__,
                                 "Error writing PNG file \"" + filepath + "\": png_create_write_struct failed !");
        }

        if (filepath == "stdout:") {
            outfile = stdout;
        } else if (filepath == "HTTP") {
            png_set_write_fn(png_ptr, img->connection(), conn_write_data, conn_flush_data);
        } else {
            if (!(outfile = fopen(filepath.c_str(), "wb"))) {
                png_free_data(png_ptr, nullptr, PNG_FREE_ALL, -1);
                throw SipiImageError(__file__, __LINE__,
                                     "Error writing PNG file \"" + filepath + "\": Could notopen output file !");
            }
        }

        png_infop info_ptr;
        if (!(info_ptr = png_create_info_struct(png_ptr))) {
            png_free_data(png_ptr, nullptr, PNG_FREE_ALL, -1);
            throw SipiImageError(__file__, __LINE__,
                                 "Error writing PNG file \"" + filepath + "\": png_create_info_struct !");
        }

        if (outfile != nullptr) png_init_io(png_ptr, outfile);

        png_set_filter(png_ptr, 0, PNG_FILTER_NONE);

        /* set the zlib compression level */
        png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);

        int color_type;
        if (img->nc == 1) { // grey value
            color_type = PNG_COLOR_TYPE_GRAY;
        } else if ((img->nc == 2) && (img->es.size() == 1)) { // grey value with alpha
            color_type = PNG_COLOR_TYPE_GRAY_ALPHA;
        } else if (img->nc == 3) { // RGB
            color_type = PNG_COLOR_TYPE_RGB;
        } else if ((img->nc == 4) && (img->es.size() == 1)) { // RGB + ALPHA
            color_type = PNG_COLOR_TYPE_RGB_ALPHA;
        }
        else if (img->nc == 4) {
            img->convertToIcc(SipiIcc(Sipi::PredefinedProfiles::icc_sRGB), 8);
            color_type = PNG_COLOR_TYPE_RGB;
            img->nc = 3;
            img->bps = 8;
        }
        else {
            png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
            throw SipiImageError(__file__, __LINE__,
                                 "Error writing PNG file \"" + filepath + "\": cannot handle number of channels () !");
        }

        png_set_IHDR(png_ptr, info_ptr, img->nx, img->ny, img->bps, color_type, PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

        //
        // ICC profile handfling is special...
        //
        SipiEssentials es = img->essential_metadata();
        if ((img->icc != nullptr) || es.use_icc()) {
            if ((img->icc != nullptr) && (img->icc->getProfileType() == icc_LAB)) {
                img->convertToIcc(SipiIcc(Sipi::PredefinedProfiles::icc_sRGB), img->bps);
            }
            std::vector<unsigned char> icc_buf;
            try {
                if (es.use_icc()) {
                    icc_buf = es.icc_profile();
                }
                else {
                    icc_buf = img->icc->iccBytes();
                }
                png_set_iCCP(png_ptr, info_ptr, "ICC", PNG_COMPRESSION_TYPE_BASE, icc_buf.data(), icc_buf.size());
            } catch (SipiError &err) {
                std::cerr << err << std::endl;
            }
        }

        PngTextPtr chunk_ptr(4);

        //
        // other metadata comes here
        //

        std::vector<unsigned char> exif_buf;
        if (img->exif) {
            exif_buf = img->exif->exifBytes();
            chunk_ptr.add_zTXt(exif_tag, (char *) exif_buf.data(), exif_buf.size());
        }

        std::vector<unsigned char> iptc_buf;
        if (img->iptc) {
            iptc_buf = img->iptc->iptcBytes();
            chunk_ptr.add_zTXt(iptc_tag, (char *) iptc_buf.data(), iptc_buf.size());
        }

        std::string xmp_buf;
        if (img->xmp != nullptr) {
            xmp_buf = img->xmp->xmpBytes();
            chunk_ptr.add_iTXt(xmp_tag, (char *) xmp_buf.data(), xmp_buf.size());
        }

        if (es.is_set()) {
            std::string esstr = es;
            unsigned int len = esstr.length();
            char sipi_buf[512 + 1];
            strncpy(sipi_buf, esstr.c_str(), 512);
            sipi_buf[512] = '\0';
            chunk_ptr.add_iTXt(sipi_tag, sipi_buf, len);
        }

        if (chunk_ptr.num() > 0) {
            png_set_text(png_ptr, info_ptr, chunk_ptr.ptr(), chunk_ptr.num());
        }

        png_bytep *row_pointers = (png_bytep *) png_malloc(png_ptr, img->ny * sizeof(png_byte *));

        if (img->bps == 8) {
            for (size_t i = 0; i < img->ny; i++) {
                row_pointers[i] = (img->pixels + i * img->nx * img->nc);
            }
        } else if (img->bps == 16) {
            for (size_t i = 0; i < img->ny; i++) {
                row_pointers[i] = (img->pixels + 2 * i * img->nx * img->nc);
            }
        }

        png_set_rows(png_ptr, info_ptr, row_pointers);

        png_write_info(png_ptr, info_ptr);
        png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_SWAP_ENDIAN,
                      nullptr); // we expect the data to be little endian...
        png_write_end(png_ptr, info_ptr);

        png_free(png_ptr, row_pointers);
        row_pointers = nullptr;

        png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
        png_destroy_write_struct(&png_ptr, &info_ptr);

        png_ptr = nullptr;
        info_ptr = nullptr;

        if (outfile != nullptr) fclose(outfile);
    }

}
