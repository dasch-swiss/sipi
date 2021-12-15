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
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cmath>
#include <syslog.h>

#include <fcntl.h>
#include <stdio.h>

#include "SipiError.h"
#include "SipiIOJpeg.h"
#include "SipiCommon.h"
#include "shttps/Connection.h"
#include "shttps/makeunique.h"

#include "jpeglib.h"
#include "jerror.h"

#define ICC_MARKER  (JPEG_APP0 + 2)    /* JPEG marker code for ICC */
#define ICC_OVERHEAD_LEN  14        /* size of non-profile data in APP2 */
#define MAX_BYTES_IN_MARKER  65533    /* maximum data len of a JPEG marker */


static const char __file__[] = __FILE__;


namespace Sipi {
    //static std::mutex inlock;

    inline bool getbyte(int &c, FILE *f) {
        if ((c = getc(f)) == EOF) {
            return false;
        }
        else {
            return true;
        }
    }

    inline bool getword(int &c, FILE *f) {
        int cc_ = 0;
        int dd_ = 0;
        if (((cc_ = getc(f)) == EOF) || ((dd_ = getc(f)) == EOF)) {
            return false;
        }
        else {
            c = (cc_ << 8) + dd_;
            return true;
        }
    }

    /*!
     * Special exception within the JPEG routines which can be caught separately
     */
    class JpegError : public std::runtime_error {
    public:
        inline JpegError() : std::runtime_error("!! JPEG_ERROR !!") {}

        inline JpegError(const char *msg) : std::runtime_error(msg) {}

        inline const char *what() const noexcept {
            return std::runtime_error::what();
        }
    };
    //------------------------------------------------------------------


    typedef struct _FileBuffer {
        JOCTET *buffer;
        size_t buflen;
        int file_id;
    } FileBuffer;

    /*!
     * Function which initializes the structures for managing the IO
     */
    static void init_file_destination(j_compress_ptr cinfo) {
        FileBuffer *file_buffer = (FileBuffer *) cinfo->client_data;
        cinfo->dest->free_in_buffer = file_buffer->buflen;
        cinfo->dest->next_output_byte = file_buffer->buffer;
    }
    //=============================================================================

    /*!
     * Function empty the libjeg buffer and write the data to the socket
     */
    static boolean empty_file_buffer(j_compress_ptr cinfo) {
        FileBuffer *file_buffer = (FileBuffer *) cinfo->client_data;
        size_t n = file_buffer->buflen;
        size_t nn = 0;
        do {
            int tmp_n = write(file_buffer->file_id, file_buffer->buffer + nn, n);
            if (tmp_n < 0) {
                throw JpegError("Couldn't write to file!");
                //throw SipiImageError(thisSourceFile, __LINE__, "Couldn't write to file!");
                //return false; // and create an error message!!
            } else {
                n -= tmp_n;
                nn += tmp_n;
            }
        } while (n > 0);

        cinfo->dest->free_in_buffer = file_buffer->buflen;
        cinfo->dest->next_output_byte = file_buffer->buffer;

        return true;
    }
    //=============================================================================

    /*!
     * Finish writing data
     */
    static void term_file_destination(j_compress_ptr cinfo) {
        FileBuffer *file_buffer = (FileBuffer *) cinfo->client_data;
        size_t n = cinfo->dest->next_output_byte - file_buffer->buffer;
        size_t nn = 0;
        do {
            auto tmp_n = write(file_buffer->file_id, file_buffer->buffer + nn, n);
            if (tmp_n < 0) {
                throw SipiImageError(__file__, __LINE__, "Couldn't write to file!");
            } else {
                n -= tmp_n;
                nn += tmp_n;
            }
        } while (n > 0);

        delete[] file_buffer->buffer;
        delete file_buffer;
        cinfo->client_data = nullptr;

        free(cinfo->dest);
        cinfo->dest = nullptr;
    }
    //=============================================================================

    /*!
     * This function is used to setup the I/O destination to the HTTP socket
     */
    static void jpeg_file_dest(struct jpeg_compress_struct *cinfo, int file_id, size_t buflen = 64 * 1024) {
        struct jpeg_destination_mgr *destmgr;
        FileBuffer *file_buffer;
        cinfo->client_data = new FileBuffer;
        file_buffer = (FileBuffer *) cinfo->client_data;

        file_buffer->buffer = new JOCTET[buflen]; //(JOCTET *) malloc(buflen*sizeof(JOCTET));
        file_buffer->buflen = buflen;
        file_buffer->file_id = file_id;

        destmgr = (struct jpeg_destination_mgr *) malloc(sizeof(struct jpeg_destination_mgr));

        destmgr->init_destination = init_file_destination;
        destmgr->empty_output_buffer = empty_file_buffer;
        destmgr->term_destination = term_file_destination;

        cinfo->dest = destmgr;
    }
    //=============================================================================


    static void init_file_source(struct jpeg_decompress_struct *cinfo) {
        FileBuffer *file_buffer = (FileBuffer *) cinfo->client_data;
        cinfo->src->next_input_byte = file_buffer->buffer;
        cinfo->src->bytes_in_buffer = 0;
    }
    //=============================================================================

    static int file_source_fill_input_buffer(struct jpeg_decompress_struct *cinfo) {
        FileBuffer *file_buffer = (FileBuffer *) cinfo->client_data;
        size_t nbytes = 0;
        do {
            auto n = read(file_buffer->file_id, file_buffer->buffer + nbytes, file_buffer->buflen - nbytes);
            if (n < 0) {
                break; // error
            }
            if (n == 0) break; // EOF reached...
            nbytes += n;
        } while (nbytes < file_buffer->buflen);
        if (nbytes <= 0) {
            ERREXIT(cinfo, 999);
            /*
            WARNMS(cinfo, JWRN_JPEG_EOF);
            infile_buffer->buffer[0] = (JOCTET) 0xFF;
            infile_buffer->buffer[1] = (JOCTET) JPEG_EOI;
            nbytes = 2;
            */
        }
        cinfo->src->next_input_byte = file_buffer->buffer;
        cinfo->src->bytes_in_buffer = nbytes;
        return TRUE;
    }
    //=============================================================================

    static void file_source_skip_input_data(struct jpeg_decompress_struct *cinfo, long num_bytes) {
        std::cerr << "file_source_skip_input_data" << std::endl;
        if (num_bytes > 0) {
            while (num_bytes > (long) cinfo->src->bytes_in_buffer) {
                num_bytes -= (long) cinfo->src->bytes_in_buffer;
                (void) file_source_fill_input_buffer(cinfo);
            }
        }
        cinfo->src->next_input_byte += (size_t) num_bytes;
        cinfo->src->bytes_in_buffer -= (size_t) num_bytes;
    }
    //=============================================================================

    static void term_file_source(struct jpeg_decompress_struct *cinfo) {
        FileBuffer *file_buffer = (FileBuffer *) cinfo->client_data;

        delete[] file_buffer->buffer;
        delete file_buffer;
        cinfo->client_data = nullptr;
        free(cinfo->src);
        cinfo->src = nullptr;
    }
    //=============================================================================

    static void jpeg_file_src(struct jpeg_decompress_struct *cinfo, int file_id, size_t buflen = 64 * 1024) {
        struct jpeg_source_mgr *srcmgr;
        FileBuffer *file_buffer;
        cinfo->client_data = new FileBuffer;
        file_buffer = (FileBuffer *) cinfo->client_data;

        file_buffer->buffer = new JOCTET[buflen];
        file_buffer->buflen = buflen;
        file_buffer->file_id = file_id;

        srcmgr = (struct jpeg_source_mgr *) malloc(sizeof(struct jpeg_source_mgr));
        srcmgr->init_source = init_file_source;
        srcmgr->fill_input_buffer = file_source_fill_input_buffer;
        srcmgr->skip_input_data = file_source_skip_input_data;
        srcmgr->resync_to_restart = jpeg_resync_to_restart; // default!
        srcmgr->term_source = term_file_source;

        cinfo->src = srcmgr;
    }
    //=============================================================================

    /*!
     * Struct that is used to hold the variables for defining the
     * private I/O routines which are used to write the the HTTP socket
     */
    typedef struct _HtmlBuffer {
        JOCTET *buffer; //!< Buffer for holding data to be written out
        size_t buflen;  //!< length of the buffer
        shttps::Connection *conobj; //!< Pointer to the connection objects
    } HtmlBuffer;

    /*!
     * Function which initializes the structures for managing the IO
     */
    static void init_html_destination(j_compress_ptr cinfo) {
        HtmlBuffer *html_buffer = (HtmlBuffer *) cinfo->client_data;
        cinfo->dest->free_in_buffer = html_buffer->buflen;
        cinfo->dest->next_output_byte = html_buffer->buffer;
    }
    //=============================================================================

    /*!
     * Function empty the libjeg buffer and write the data to the socket
     */
    static boolean empty_html_buffer(j_compress_ptr cinfo) {
        HtmlBuffer *html_buffer = (HtmlBuffer *) cinfo->client_data;
        try {
            html_buffer->conobj->sendAndFlush(html_buffer->buffer, html_buffer->buflen);
        } catch (int i) { // an error occurred (possibly a broken pipe)
            throw JpegError("Couldn't write to HTTP socket");
            //return false;
        }
        cinfo->dest->free_in_buffer = html_buffer->buflen;
        cinfo->dest->next_output_byte = html_buffer->buffer;

        return true;
    }
    //=============================================================================

    /*!
     * Finish writing data
     */
    static void term_html_destination(j_compress_ptr cinfo) {
        std::cerr << "term_html_destination(j_compress_ptr cinfo)" << std::endl;
        HtmlBuffer *html_buffer = (HtmlBuffer *) cinfo->client_data;
        size_t nbytes = cinfo->dest->next_output_byte - html_buffer->buffer;
        try {
            html_buffer->conobj->sendAndFlush(html_buffer->buffer, nbytes);
        } catch (int i) { // an error occured in sending the data (broken pipe?)
            throw JpegError("Couldn't write to HTTP socket");
        }

        free(html_buffer->buffer);
        free(html_buffer);
        cinfo->client_data = nullptr;

        free(cinfo->dest);
        cinfo->dest = nullptr;
    }
    //=============================================================================

    static void cleanup_html_destination(j_compress_ptr cinfo) {
        HtmlBuffer *html_buffer = (HtmlBuffer *) cinfo->client_data;
        free(html_buffer->buffer);
        free(html_buffer);
        cinfo->client_data = nullptr;

        free(cinfo->dest);
        cinfo->dest = nullptr;
    }

    /*!
     * This function is used to setup the I/O destination to the HTTP socket
     */
    static void
    jpeg_html_dest(struct jpeg_compress_struct *cinfo, shttps::Connection *conobj, size_t buflen = 64 * 1024) {
        struct jpeg_destination_mgr *destmgr;
        HtmlBuffer *html_buffer;
        cinfo->client_data = malloc(sizeof(HtmlBuffer));
        html_buffer = (HtmlBuffer *) cinfo->client_data;

        html_buffer->buffer = (JOCTET *) malloc(buflen * sizeof(JOCTET));
        html_buffer->buflen = buflen;
        html_buffer->conobj = conobj;

        destmgr = (struct jpeg_destination_mgr *) malloc(sizeof(struct jpeg_destination_mgr));

        destmgr->init_destination = init_html_destination;
        destmgr->empty_output_buffer = empty_html_buffer;
        destmgr->term_destination = term_html_destination;

        cinfo->dest = destmgr;
    }
    //=============================================================================

    void SipiIOJpeg::parse_photoshop(SipiImage *img, char *data, int length) {
        int slen = 0;
        unsigned int datalen = 0;
        char *ptr = data;
        unsigned short id;
        char sig[5];
        char name[256];
        int i;

        //cerr << "Parse photoshop: TOTAL LENGTH = " << length << endl;

        while ((ptr - data) < length) {
            sig[0] = *ptr;
            sig[1] = *(ptr + 1);
            sig[2] = *(ptr + 2);
            sig[3] = *(ptr + 3);
            sig[4] = '\0';
            if (strcmp(sig, "8BIM") != 0) break;
            ptr += 4;

            //
            // tag-ID processing
            id = ((unsigned char) *(ptr + 0) << 8) | (unsigned char) *(ptr + 1); // ID
            ptr += 2; // ID

            //
            // name processing (Pascal string)
            slen = *ptr;
            for (i = 0; (i < slen) && (i < 256); i++) name[i] = *(ptr + i + 1);
            name[i] = '\0';
            slen++; // add length byte
            if ((slen % 2) == 1) slen++;
            ptr += slen;

            //
            // data processing
            datalen = ((unsigned char) *ptr << 24) | ((unsigned char) *(ptr + 1) << 16) |
                      ((unsigned char) *(ptr + 2) << 8) | (unsigned char) *(ptr + 3);

            ptr += 4;

            switch (id) {
                case 0x0404: { // IPTC data
                    //cerr << ">>> Photoshop: IPTC" << endl;
                    if (img->iptc == nullptr) img->iptc = std::make_shared<SipiIptc>((unsigned char *) ptr, datalen);
                    // IPTC – handled separately!
                    break;
                }
                case 0x040f: { // ICC data
                    //cerr << ">>> Photoshop: ICC" << endl;
                    // ICC profile
                    if (img->icc == nullptr) img->icc = std::make_shared<SipiIcc>((unsigned char *) ptr, datalen);
                    break;
                }
                case 0x0422: { // EXIF data
                    if (img->exif == nullptr) img->exif = std::make_shared<SipiExif>((unsigned char *) ptr, datalen);
                    //cerr << ">>> Photoshop: EXIF" << endl;
                    // exif
                    break;
                }
                case 0x0424: { // XMP data
                    //cerr << ">>> Photoshop: XMP" << endl;
                    // XMP data
                    if (img->xmp == nullptr) img->xmp = std::make_shared<SipiXmp>(ptr, datalen);
                }
                default: {
                    // URL
                    char *str = (char *) calloc(1, (datalen + 1) * sizeof(char));
                    memcpy(str, ptr, datalen);
                    str[datalen] = '\0';
                    //fprintf(stderr, "XXX=%s\n", str);
                    break;
                }
            }

            if ((datalen % 2) == 1) datalen++;
            ptr += datalen;
        }
    }
    //=============================================================================


    /*!
    * This function is used to catch libjpeg errors which otherwise would
    * result in a call exit()
    */
    static void jpegErrorExit(j_common_ptr cinfo) {
        char jpegLastErrorMsg[JMSG_LENGTH_MAX];
        /* Create the message */
        (*(cinfo->err->format_message))(cinfo, jpegLastErrorMsg);
        /* Jump to the setjmp point */
        throw JpegError(jpegLastErrorMsg);
    }
    //=============================================================================


    bool SipiIOJpeg::read(SipiImage *img, std::string filepath, int pagenum, std::shared_ptr<SipiRegion> region,
                          std::shared_ptr<SipiSize> size, bool force_bps_8,
                          ScalingQuality scaling_quality)
    {
        int infile;
        //
        // open the input file
        //
        if ((infile = ::open(filepath.c_str(), O_RDONLY)) == -1) {
            return false;
        }
        // workaround for bug #0011: jpeglib crashes the app when the file is not a jpeg file
        // we check the magic number before calling any jpeglib routines
        unsigned char magic[2];
        if (::read(infile, magic, 2) != 2) {
            return false;
        }
        if ((magic[0] != 0xff) || (magic[1] != 0xd8)) {
            close(infile);
            return false; // it's not a JPEG file!
        }
        // move infile position back to the beginning of the file
        ::lseek(infile, 0, SEEK_SET);

        //
        // Since libjpeg is not thread safe, we have unfortunately use a mutex...
        //
        //std::lock_guard<std::mutex> inlock_mutex_guard(inlock);

        struct jpeg_decompress_struct cinfo;
        struct jpeg_error_mgr jerr;


        JSAMPARRAY linbuf = nullptr;
        jpeg_saved_marker_ptr marker;

        //
        // let's create the decompressor
        //
        jpeg_create_decompress (&cinfo);

        cinfo.dct_method = JDCT_FLOAT;

        cinfo.err = jpeg_std_error(&jerr);
        jerr.error_exit = jpegErrorExit;

        try {
            //jpeg_stdio_src(&cinfo, infile);
            jpeg_file_src(&cinfo, infile);
            jpeg_save_markers(&cinfo, JPEG_COM, 0xffff);
            for (int i = 0; i < 16; i++) {
                jpeg_save_markers(&cinfo, JPEG_APP0 + i, 0xffff);
            }
        } catch (JpegError &jpgerr) {
            jpeg_destroy_decompress(&cinfo);
            close(infile);
            throw SipiError(__file__, __LINE__, "Error reading JPEG file: \"" + filepath + "\": " + jpgerr.what());
        }

        //
        // now we read the header
        //
        int res;
        try {
            res = jpeg_read_header(&cinfo, TRUE);
        } catch (JpegError &jpgerr) {
            jpeg_destroy_decompress(&cinfo);
            close(infile);
            throw SipiImageError(__file__, __LINE__, "Error reading JPEG file: \"" + filepath + "\": " + jpgerr.what());
        }
        if (res != JPEG_HEADER_OK) {
            jpeg_destroy_decompress(&cinfo);
            close(infile);
            throw SipiImageError(__file__, __LINE__, "Error reading JPEG file: \"" + filepath + "\"");
        }

        boolean no_cropping = false;
        if (region == nullptr) no_cropping = true;
        if ((region != nullptr) && (region->getType()) == SipiRegion::FULL) no_cropping = true;

        size_t nnx, nny;
        SipiSize::SizeType rtype = SipiSize::FULL;
        if (size != nullptr) {
            rtype = size->getType();
        }

        if (no_cropping) {
            //
            // here we prepare tha scaling/reduce stuff...
            //
            int reduce = 3; // maximal reduce factor is 3: 1/1, 1/2, 1/4 and 1/8
            bool redonly = true; // we assume that only a reduce is necessary
            if ((size != nullptr) && (rtype != SipiSize::FULL)) {
                size->get_size(cinfo.image_width, cinfo.image_height, nnx, nny, reduce, redonly);
            }
            else {
                reduce = 0;
            }

            if (reduce < 0) reduce = 0;
            cinfo.scale_num = 1;
            cinfo.scale_denom = 1;
            for (int i = 0; i < reduce; i++) cinfo.scale_denom *= 2;
        }
        cinfo.do_fancy_upsampling = false;


        //
        // getting Metadata
        //
        marker = cinfo.marker_list;
        unsigned char *icc_buffer = nullptr;
        int icc_buffer_len = 0;
        while (marker) {
            if (marker->marker == JPEG_COM) {
                std::string emdatastr((char *) marker->data, marker->data_length);
                if (emdatastr.compare(0, 5, "SIPI:", 5) == 0) {
                    SipiEssentials se(emdatastr);
                    img->essential_metadata(se);
                }
            } else if (marker->marker == JPEG_APP0 + 1) { // EXIF, XMP MARKER....
                //
                // first we try to find the exif part
                //
                unsigned char *pos = (unsigned char *) memmem(marker->data, marker->data_length, "Exif\000\000", 6);
                if (pos != nullptr) {
                    img->exif = std::make_shared<SipiExif>(pos + 6, marker->data_length - (pos - marker->data) - 6);
                }

                //
                // first we try to find the xmp part: TODO: reading XMP which spans multiple segments. See ExtendedXMP !!!
                //
                pos = (unsigned char *) memmem(marker->data, marker->data_length, "http://ns.adobe.com/xap/1.0/\000",
                                               29);
                if (pos != nullptr) {
                    try {
                        char start[] = {'<', '?', 'x', 'p', 'a', 'c', 'k', 'e', 't', ' ', 'b', 'e', 'g', 'i', 'n',
                                        '\0'};
                        char end[] = {'<', '?', 'x', 'p', 'a', 'c', 'k', 'e', 't', ' ', 'e', 'n', 'd', '\0'};

                        char *s;
                        unsigned int ll = 0;
                        do {
                            s = start;
                            // skip to the start marker
                            while ((ll < marker->data_length) && (*pos != *s)) {
                                pos++; //// ISSUE: code failes here if there are many concurrent access; data overrrun??
                                ll++;
                            }
                            // read the start marker
                            while ((ll < marker->data_length) && (*s != '\0') && (*pos == *s)) {
                                pos++;
                                s++;
                                ll++;
                            }
                        } while ((ll < marker->data_length) && (*s != '\0'));
                        if (ll == marker->data_length) {
                            // we didn't find anything....
                            throw SipiError(__file__, __LINE__, "XMP Problem");
                        }
                        // now we start reading the data
                        while ((ll < marker->data_length) && (*pos != '>')) {
                            ll++;
                            pos++;
                        }
                        pos++; // finally we have the start of XMP string
                        unsigned char *start_xmp = pos;

                        unsigned char *end_xmp;
                        do {
                            s = end;
                            while (*pos != *s) pos++;
                            end_xmp = pos; // a candidate
                            while ((*s != '\0') && (*pos == *s)) {
                                pos++;
                                s++;
                            }
                        } while (*s != '\0');
                        while (*pos != '>') {
                            pos++;
                        }
                        pos++;

                        size_t xmp_len = end_xmp - start_xmp;

                        std::string xmpstr((char *) start_xmp, xmp_len);
                        size_t npos = xmpstr.find("</x:xmpmeta>");
                        xmpstr = xmpstr.substr(0, npos + 12);

                        img->xmp = std::make_shared<SipiXmp>(xmpstr);
                    } catch (SipiError &err) {
                        std::cerr << "Failed to parse XMP..." << std::endl;
                    }
                }
            } else if (marker->marker == JPEG_APP0 + 2) { // ICC MARKER.... may span multiple marker segments
                //
                // first we try to find the exif part
                //
                unsigned char *pos = (unsigned char *) memmem(marker->data, marker->data_length, "ICC_PROFILE\0", 12);
                if (pos != nullptr) {
                    auto len = marker->data_length - (pos - (unsigned char *) marker->data) - 14;
                    icc_buffer = (unsigned char *) realloc(icc_buffer, icc_buffer_len + len);
                    Sipi::memcpy(icc_buffer + icc_buffer_len, pos + 14, (size_t) len);
                    icc_buffer_len += len;
                }
            } else if (marker->marker == JPEG_APP0 + 13) { // PHOTOSHOP MARKER....
                if (strncmp("Photoshop 3.0", (char *) marker->data, 14) == 0) {
                    parse_photoshop(img, (char *) marker->data + 14, (int) marker->data_length - 14);
                }
            } else {
                //fprintf(stderr, "4) MARKER= %d, %d Bytes, ==> %s\n\n", marker->marker - JPEG_APP0, marker->data_length, marker->data);
            }
            marker = marker->next;
        }
        if (icc_buffer != nullptr) {
            img->icc = std::make_shared<SipiIcc>(icc_buffer, icc_buffer_len);
        }

        try {
            jpeg_start_decompress(&cinfo);
        } catch (JpegError &jpgerr) {
            jpeg_destroy_decompress(&cinfo);
            close(infile);
            throw SipiImageError(__file__, __LINE__, "Error reading JPEG file: \"" + filepath + "\": " + jpgerr.what());
        }

        img->bps = 8;
        img->nx = cinfo.output_width;
        img->ny = cinfo.output_height;
        img->nc = cinfo.output_components;
        int colspace = cinfo.out_color_space; // JCS_UNKNOWN, JCS_GRAYSCALE, JCS_RGB, JCS_YCbCr, JCS_CMYK, JCS_YCCK
        switch (colspace) {
            case JCS_RGB: {
                img->photo = RGB;
                break;
            }
            case JCS_GRAYSCALE: {
                img->photo = MINISBLACK;
                break;
            }
            case JCS_CMYK: {
                img->photo = SEPARATED;
                break;
            }
            case JCS_YCbCr: {
                img->photo = YCBCR;
                break;
            }
            case JCS_YCCK: {
                throw SipiImageError(__file__, __LINE__, "Unsupported JPEG colorspace (JCS_YCCK)!");
            }
            case JCS_UNKNOWN: {
                throw SipiImageError(__file__, __LINE__, "Unsupported JPEG colorspace (JCS_UNKNOWN)!");
            }
            default: {
                throw SipiImageError(__file__, __LINE__, "Unsupported JPEG colorspace!");
            }
        }
        int sll = cinfo.output_components * cinfo.output_width * sizeof(uint8);

        img->pixels = new byte[img->ny * sll];

        try {
            linbuf = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, sll, 1);
            for (size_t i = 0; i < img->ny; i++) {
                jpeg_read_scanlines(&cinfo, linbuf, 1);
                memcpy(&(img->pixels[i * sll]), linbuf[0], (size_t) sll);
            }
        } catch (JpegError &jpgerr) {
            jpeg_destroy_decompress(&cinfo);
            close(infile);
            throw SipiImageError(__file__, __LINE__, "Error reading JPEG file: \"" + filepath + "\": " + jpgerr.what());
        }
        try {
            jpeg_finish_decompress(&cinfo);
        } catch (JpegError &jpgerr) {
            close(infile);
            throw SipiImageError(__file__, __LINE__, "Error reading JPEG file: \"" + filepath + "\": " + jpgerr.what());
        }

        try {
            jpeg_destroy_decompress(&cinfo);
        } catch (JpegError &jpgerr) {
            close(infile);
            //inlock.unlock();
            throw SipiImageError(__file__, __LINE__, "Error reading JPEG file: \"" + filepath + "\": " + jpgerr.what());
        }
        close(infile);


        //
        // do some cropping...
        //
        if (!no_cropping) { // not no cropping (!!) means "do crop"!
            //
            // let's first crop the region (we read the full size image in this case)
            //
            (void) img->crop(region);

            //
            // no we scale the region to the desired size
            //
            int reduce = -1;
            bool redonly;
            (void) size->get_size(img->nx, img->ny, nnx, nny, reduce, redonly);
        }

        //
        // resize/Scale the image if necessary
        //
        if ((size != NULL) && (rtype != SipiSize::FULL)) {
            if (rtype != SipiSize::FULL) {
                switch (scaling_quality.jpeg) {
                    case HIGH: img->scale(nnx, nny);
                        break;
                    case MEDIUM: img->scaleMedium(nnx, nny);
                        break;
                    case LOW: img->scaleFast(nnx, nny);
                        break;
                }
            }
        }

        return TRUE;
    }
    //============================================================================


#   define readbyte(a, b) do if(((a)=getc((b))) == EOF) return 0; while (0)
#   define readword(a, b) do { int cc_=0,dd_=0; \
                              if((cc_=getc((b))) == EOF \
                              || (dd_=getc((b))) == EOF) return 0; \
                              (a) = (cc_<<8) + (dd_); \
                          } while(0)


    SipiImgInfo SipiIOJpeg::getDim(std::string filepath, int pagenum) {
        // portions derived from IJG code */

        FILE *infile;
        SipiImgInfo info;

        //
        // open the input file
        //
        if ((infile = fopen(filepath.c_str(), "rb")) == nullptr) {
            // inlock.unlock();
            info.success = SipiImgInfo::FAILURE;
            return info;
        }

        int marker = 0;
        int dummy = 0;
        if (getc(infile) != 0xFF || getc(infile) != 0xD8) {
            fclose(infile);
            info.success = SipiImgInfo::FAILURE;
            return info;
        }
        for (;;) {
            int discarded_bytes = 0;
            if (!getbyte(marker, infile)) {
                info.success = SipiImgInfo::FAILURE;
                return info;
            }
            while (marker != 0xFF) {
                discarded_bytes++;
                if (!getbyte(marker, infile)) {
                    info.success = SipiImgInfo::FAILURE;
                    return info;
                }
            }
            do {
                if (!getbyte(marker, infile)) {
                    info.success = SipiImgInfo::FAILURE;
                    return info;
                }
            } while (marker == 0xFF);

            if (discarded_bytes != 0) {
                fclose(infile);
                info.success = SipiImgInfo::FAILURE;
                return info;
            }

            switch (marker) {
                case 0xC0:
                case 0xC1:
                case 0xC2:
                case 0xC3:
                case 0xC5:
                case 0xC6:
                case 0xC7:
                case 0xC9:
                case 0xCA:
                case 0xCB:
                case 0xCD:
                case 0xCE:
                case 0xCF: {
                    if (!getword(dummy, infile)) { /* usual parameter length count */
                        info.success = SipiImgInfo::FAILURE;
                        return info;
                    }
                    if (!getbyte(dummy, infile)) {
                        info.success = SipiImgInfo::FAILURE;
                        return info;
                    }
                    int tmp_height;
                    if (!getword(tmp_height, infile)) {
                        info.success = SipiImgInfo::FAILURE;
                        return info;
                    }
                    info.height = tmp_height;
                    int tmp_width;
                    if (!getword(tmp_width, infile)) {
                        info.success = SipiImgInfo::FAILURE;
                        return info;
                    }
                    info.width = tmp_width;
                    info.success = SipiImgInfo::DIMS;
                    if (!getbyte(dummy, infile)) {
                        info.success = SipiImgInfo::FAILURE;
                        return info;
                    }
                    fclose(infile);
                    return info;
                }
                case 0xDA:
                case 0xD9:
                    fclose(infile);
                    info.success = SipiImgInfo::FAILURE;
                    return info;
                default: {
                    int length;
                    if (!getword(length, infile)) {
                        info.success = SipiImgInfo::FAILURE;
                        return info;
                    }
                    if (length < 2) {
                        fclose(infile);
                        info.success = SipiImgInfo::FAILURE;
                        return info;
                    }
                    length -= 2;
                    while (length > 0) {
                        if (!getbyte(dummy, infile)) {
                            info.success = SipiImgInfo::FAILURE;
                            return info;
                        }
                        length--;
                    }
                }
                break;
            }
        }
        info.success = SipiImgInfo::FAILURE;
        return info;
    }
    //============================================================================


    void SipiIOJpeg::write(SipiImage *img, std::string filepath, const SipiCompressionParams *params) {
        int quality = 80;
        if ((params != nullptr) && (!params->empty())) {
            try {
                quality = stoi(params->at(JPEG_QUALITY));
            }
            catch(const std::out_of_range &er) {
                throw SipiImageError(__file__, __LINE__, "JPEG quality argument must be integer between 0 and 100");
            }
            catch (const std::invalid_argument& ia) {
                throw SipiImageError(__file__, __LINE__, "JPEG quality argument must be integer between 0 and 100");
            }
            if ((quality < 0) || (quality > 100)){
                throw SipiImageError(__file__, __LINE__, "JPEG quality argument must be integer between 0 and 100");
            }
        }

        if (img->bps == 16) img->to8bps();

        //
        // we have to check if the image has an alpha channel (not supported by JPEG). If
        // so, we remove it!
        //
        if ((img->getNc() > 3) && (img->getNalpha() > 0)) { // we have an alpha channel....
            for (size_t i = 3; i < (img->getNalpha() + 3); i++) img->removeChan(i);
        }

        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr jerr;

        cinfo.err = jpeg_std_error(&jerr);
        jerr.error_exit = jpegErrorExit;

        int outfile = -1;        /* target file */
        JSAMPROW row_pointer[1];    /* pointer to JSAMPLE row[s] */
        int row_stride;        /* physical row width in image buffer */

        try {
            jpeg_create_compress(&cinfo);
        } catch (JpegError &jpgerr) {
            throw SipiImageError(__file__, __LINE__, jpgerr.what());
        }
        if (filepath == "HTTP") { // we are transmitting the data through the webserver
            shttps::Connection *conobj = img->connection();
            try {
                jpeg_html_dest(&cinfo, conobj);
            } catch (JpegError &jpgerr) {
                jpeg_destroy_compress(&cinfo);
                std::cerr << "BEFORE jpeg_destroy_compress(&cinfo)" << std::endl;
                jpeg_destroy_compress(&cinfo);
                std::cerr << "AFTER jpeg_destroy_compress(&cinfo)" << std::endl;
                cleanup_html_destination(&cinfo);
                throw SipiImageError(__file__, __LINE__, jpgerr.what());
            }
        } else {
            if (filepath == "stdout:") {
                jpeg_stdio_dest(&cinfo, stdout);
            } else {
                if ((outfile = open(filepath.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) ==
                    -1) {
                    throw SipiImageError(__file__, __LINE__, "Cannot open file \"" + filepath + "\"!");
                }
                jpeg_file_dest(&cinfo, outfile);
            }
        }

        cinfo.image_width = (int) img->nx;    /* image width and height, in pixels */
        cinfo.image_height = (int) img->ny;
        cinfo.input_components = (int) img->nc;        /* # of color components per pixel */
        switch (img->photo) {
            case MINISWHITE:
            case MINISBLACK: {
                if (img->nc != 1) {
                    throw SipiImageError(__file__, __LINE__,
                                         "Num of components not 1 (nc = " + std::to_string(img->nc) + ")!");
                }
                cinfo.in_color_space = JCS_GRAYSCALE;
                cinfo.jpeg_color_space = JCS_GRAYSCALE;
                break;
            }
            case RGB: {
                if (img->nc != 3) {
                    throw SipiImageError(__file__, __LINE__,
                                         "Num of components not 3 (nc = " + std::to_string(img->nc) + ")!");
                }
                cinfo.in_color_space = JCS_RGB;
                cinfo.jpeg_color_space = JCS_RGB;
                break;
            }
            case SEPARATED: {
                if (img->nc != 4) {
                    throw SipiImageError(__file__, __LINE__,
                                         "Num of components not 3 (nc = " + std::to_string(img->nc) + ")!");
                }
                cinfo.in_color_space = JCS_CMYK;
                cinfo.jpeg_color_space = JCS_CMYK;
                break;
            }
            case YCBCR: {
                if (img->nc != 3) {
                    throw SipiImageError(__file__, __LINE__,
                                         "Num of components not 3 (nc = " + std::to_string(img->nc) + ")!");
                }
                cinfo.in_color_space = JCS_YCbCr;
                cinfo.jpeg_color_space = JCS_YCbCr;
                break;
            }
            case CIELAB: {
                img->convertToIcc(Sipi::icc_sRGB, 8);
                cinfo.in_color_space = JCS_RGB;
                cinfo.jpeg_color_space = JCS_RGB;
                break;
            }
            default: {
                throw SipiImageError(__file__, __LINE__, "Unsupported JPEG colorspace: " + std::to_string(img->photo));
            }
        }
        cinfo.progressive_mode = TRUE;
        cinfo.write_Adobe_marker = TRUE;
        cinfo.write_JFIF_header = TRUE;
        try {
            jpeg_set_defaults(&cinfo);
            jpeg_set_quality(&cinfo, quality, TRUE /* TRUE, then limit to baseline-JPEG values */);

            jpeg_simple_progression(&cinfo);
            jpeg_start_compress(&cinfo, TRUE);
        } catch (JpegError &jpgerr) {
            jpeg_finish_compress(&cinfo);
            jpeg_destroy_compress(&cinfo);
            if (outfile != -1) close(outfile);
            //outlock.unlock();
            throw SipiImageError(__file__, __LINE__, jpgerr.what());
        }

        //
        // Here we write the marker
        //
        //
        //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        // ATTENTION: The markers must be written in the right sequence: APP0, APP1, APP2, ..., APP15
        //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        //

        if (img->exif != nullptr) {
            std::vector<unsigned char> buf = img->exif->exifBytes();
            if (buf.size() <= 65535) {
                char start[] = "Exif\000\000";
                size_t start_l = sizeof(start) - 1;  // remove trailing '\0';
                auto exifchunk = shttps::make_unique<unsigned char[]>(buf.size() + start_l);
                Sipi::memcpy(exifchunk.get(), start, (size_t) start_l);
                Sipi::memcpy(exifchunk.get() + start_l, buf.data(), (size_t) buf.size());

                try {
                    jpeg_write_marker(&cinfo, JPEG_APP0 + 1, (JOCTET *) exifchunk.get(), start_l + buf.size());
                } catch (JpegError &jpgerr) {
                    jpeg_finish_compress(&cinfo);
                    jpeg_destroy_compress(&cinfo);
                    if (outfile != -1) close(outfile);
                    throw SipiImageError(__file__, __LINE__, jpgerr.what());
                }
            } else {
                // std::cerr << "exif to big" << std::endl;
            }
        }

        if (img->xmp != nullptr) {
            std::string buf = img->xmp->xmpBytes();

            if ((!buf.empty()) && (buf.size() <= 65535)) {
                char start[] = "http://ns.adobe.com/xap/1.0/\000";
                size_t start_l = sizeof(start) - 1; // remove trailing '\0';
                auto xmpchunk = shttps::make_unique<char[]>(buf.size() + start_l);
                Sipi::memcpy(xmpchunk.get(), start, (size_t) start_l);
                Sipi::memcpy(xmpchunk.get() + start_l, buf.data(), (size_t) buf.size());
                try {
                    jpeg_write_marker(&cinfo, JPEG_APP0 + 1, (JOCTET *) xmpchunk.get(), start_l + buf.size());
                } catch (JpegError &jpgerr) {
                    jpeg_finish_compress(&cinfo);
                    jpeg_destroy_compress(&cinfo);
                    if (outfile != -1) close(outfile);
                    throw SipiImageError(__file__, __LINE__, jpgerr.what());
                }
            } else {
                // std::cerr << "xml to big" << std::endl;
            }
        }

        SipiEssentials es = img->essential_metadata();

        if ((img->icc != nullptr) || es.use_icc()) {
            std::vector<unsigned char> buf;
            try {
                if (es.use_icc()) {
                    buf = es.icc_profile();
                } else {
                    buf = img->icc->iccBytes();
                }
            } catch (SipiError &err) {
                std::cerr << err << std::endl;
            }
            unsigned char start[14] = {0x49, 0x43, 0x43, 0x5F, 0x50, 0x52, 0x4F, 0x46, 0x49, 0x4C, 0x45,
                                       0x0}; //"ICC_PROFILE\000";
            size_t start_l = 14;
            unsigned int n = buf.size() / (65533 - start_l + 1) + 1;

            auto iccchunk = shttps::make_unique<unsigned char[]>(65533);

            unsigned int n_towrite = buf.size();
            unsigned int n_nextwrite = 65533 - start_l;
            unsigned int n_written = 0;
            for (unsigned int i = 0; i < n; i++) {
                start[12] = (unsigned char) (i + 1);
                start[13] = (unsigned char) n;
                if (n_nextwrite > n_towrite) n_nextwrite = n_towrite;
                Sipi::memcpy(iccchunk.get(), start, (size_t) start_l);
                Sipi::memcpy(iccchunk.get() + start_l, buf.data() + n_written, (size_t) n_nextwrite);
                try {
                    jpeg_write_marker(&cinfo, ICC_MARKER, iccchunk.get(), n_nextwrite + start_l);
                } catch (JpegError &jpgerr) {
                    jpeg_finish_compress(&cinfo);
                    jpeg_destroy_compress(&cinfo);
                    if (outfile != -1) close(outfile);
                    throw SipiImageError(__file__, __LINE__, jpgerr.what());
                }

                n_towrite -= n_nextwrite;
                n_written += n_nextwrite;
            }
            if (n_towrite != 0) {
                std::cerr << "Hoppla!" << std::endl;
            }
        }

        if (img->iptc != nullptr) {
            std::vector<unsigned char> buf = img->iptc->iptcBytes();
            if (buf.size() <= 65535) {
                char start[] = " Photoshop 3.0\0008BIM\004\004\000\000";
                size_t start_l = sizeof(start) - 1;
                unsigned char siz[4];
                siz[0] = (unsigned char) ((buf.size() >> 24) & 0x000000ff);
                siz[1] = (unsigned char) ((buf.size() >> 16) & 0x000000ff);
                siz[2] = (unsigned char) ((buf.size() >> 8) & 0x000000ff);
                siz[3] = (unsigned char) (buf.size() & 0x000000ff);

                auto iptcchunk = shttps::make_unique<char[]>(start_l + 4 + buf.size());
                Sipi::memcpy(iptcchunk.get(), start, (size_t) start_l);
                Sipi::memcpy(iptcchunk.get() + start_l, siz, (size_t) 4);
                Sipi::memcpy(iptcchunk.get() + start_l + 4, buf.data(), (size_t) buf.size());

                try {
                    jpeg_write_marker(&cinfo, JPEG_APP0 + 13, (JOCTET *) iptcchunk.get(), start_l + buf.size());
                } catch (JpegError &jpgerr) {
                    jpeg_destroy_compress(&cinfo);
                    if (outfile != -1) close(outfile);
                    throw SipiImageError(__file__, __LINE__, jpgerr.what());
                }
            }
            else {
            // std::cerr << "iptc to big" << std::endl;
            }
        }

        if (es.is_set()) {
            try {
                std::string esstr = es;
                unsigned int len = esstr.length();
                char sipi_buf[512 + 1];
                strncpy(sipi_buf, esstr.c_str(), 512);
                sipi_buf[512] = '\0';
                jpeg_write_marker(&cinfo, JPEG_COM, (JOCTET *) sipi_buf, len);
            } catch (JpegError &jpgerr) {
                jpeg_destroy_compress(&cinfo);
                if (outfile != -1) close(outfile);
                throw SipiImageError(__file__, __LINE__, jpgerr.what());
            }
        }

        row_stride = img->nx * img->nc;    /* JSAMPLEs per row in image_buffer */

        try {
            while (cinfo.next_scanline < cinfo.image_height) {
                // jpeg_write_scanlines expects an array of pointers to scanlines.
                // Here the array is only one element long, but you could pass
                // more than one scanline at a time if that's more convenient.
                row_pointer[0] = &img->pixels[cinfo.next_scanline * row_stride];
                (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
            }
        } catch (JpegError &jpgerr) {
            jpeg_destroy_compress(&cinfo);
            if (outfile != -1) close(outfile);
            throw SipiImageError(__file__, __LINE__, jpgerr.what());
        }

        try {
            jpeg_finish_compress(&cinfo);
        } catch (JpegError &jpgerr) {
            jpeg_destroy_compress(&cinfo);
            if (outfile != -1) close(outfile);
            throw SipiImageError(__file__, __LINE__, jpgerr.what());
        }
        if (outfile != -1) close(outfile);

        jpeg_destroy_compress(&cinfo);

    }

} // namespace
