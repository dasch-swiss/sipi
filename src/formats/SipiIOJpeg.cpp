/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>

#include <cstdio>
#include <fcntl.h>

#include <tiff.h>

#include "shttps/Connection.h"
#include "shttps/makeunique.h"

#include "SipiCommon.h"
#include "SipiError.hpp"
#include "SipiIO.h"
#include "SipiImage.hpp"
#include "SipiImageError.hpp"
#include "formats/SipiIOJpeg.h"

#include "jerror.h"
#include "jpeglib.h"

#define ICC_MARKER (JPEG_APP0 + 2) /* JPEG marker code for ICC */
// #define ICC_OVERHEAD_LEN  14        /* size of non-profile data in APP2 */
// #define MAX_BYTES_IN_MARKER  65533    /* maximum data len of a JPEG marker */


namespace Sipi {
// static std::mutex inlock;

inline bool getbyte(int &c, FILE *f)
{
  if ((c = getc(f)) == EOF) {
    return false;
  } else {
    return true;
  }
}

inline bool getword(int &c, FILE *f)
{
  int cc_;
  int dd_;
  if (((cc_ = getc(f)) == EOF) || ((dd_ = getc(f)) == EOF)) {
    return false;
  } else {
    c = (cc_ << 8) + dd_;
    return true;
  }
}

/*!
 * Special exception within the JPEG routines which can be caught separately
 */
class JpegError : public std::runtime_error
{
public:
  inline JpegError() : std::runtime_error("!! JPEG_ERROR !!") {}

  inline explicit JpegError(const char *msg) : std::runtime_error(msg) {}

  inline const char *what() const noexcept override { return std::runtime_error::what(); }
};

//------------------------------------------------------------------


typedef struct FileBuffer
{
  JOCTET *buffer;
  size_t buflen;
  int file_id;
} FileBuffer;

/*!
 * Function which initializes the structures for managing the IO
 */
static void init_file_destination(j_compress_ptr cinfo)
{
  auto *file_buffer = (FileBuffer *)cinfo->client_data;
  cinfo->dest->free_in_buffer = file_buffer->buflen;
  cinfo->dest->next_output_byte = file_buffer->buffer;
}

//=============================================================================

/*!
 * Function empty the libjeg buffer and write the data to the socket
 */
static boolean empty_file_buffer(j_compress_ptr cinfo)
{
  auto *file_buffer = (FileBuffer *)cinfo->client_data;
  size_t n = file_buffer->buflen;
  size_t nn = 0;
  do {
    ssize_t tmp_n = write(file_buffer->file_id, file_buffer->buffer + nn, n);
    if (tmp_n < 0) {
      throw JpegError("Couldn't write to file!");
      // throw SipiImageError(thisSourceFile, __LINE__, "Couldn't write to file!");
      // return false; // and create an error message!!
    } else {
      n -= tmp_n;
      nn += tmp_n;
    }
  } while (n > 0);

  cinfo->dest->free_in_buffer = file_buffer->buflen;
  cinfo->dest->next_output_byte = file_buffer->buffer;

  return static_cast<boolean>(1);
}

//=============================================================================

/*!
 * Finish writing data
 */
static void term_file_destination(j_compress_ptr cinfo)
{
  FileBuffer *file_buffer = (FileBuffer *)cinfo->client_data;
  size_t n = cinfo->dest->next_output_byte - file_buffer->buffer;
  size_t nn = 0;
  do {
    auto tmp_n = write(file_buffer->file_id, file_buffer->buffer + nn, n);
    if (tmp_n < 0) {
      throw SipiImageError("Couldn't write to file!");
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
static void jpeg_file_dest(struct jpeg_compress_struct *cinfo, int file_id)
{
  struct jpeg_destination_mgr *destmgr;
  FileBuffer *file_buffer;
  cinfo->client_data = new FileBuffer;
  file_buffer = (FileBuffer *)cinfo->client_data;

  file_buffer->buffer = new JOCTET[65536];
  //(JOCTET *) malloc(buflen*sizeof(JOCTET));
  file_buffer->buflen = 65536;
  file_buffer->file_id = file_id;

  destmgr = (struct jpeg_destination_mgr *)malloc(sizeof(struct jpeg_destination_mgr));

  destmgr->init_destination = init_file_destination;
  destmgr->empty_output_buffer = empty_file_buffer;
  destmgr->term_destination = term_file_destination;

  cinfo->dest = destmgr;
}

//=============================================================================


static void init_file_source(struct jpeg_decompress_struct *cinfo)
{
  auto *file_buffer = (FileBuffer *)cinfo->client_data;
  cinfo->src->next_input_byte = file_buffer->buffer;
  cinfo->src->bytes_in_buffer = 0;
}

//=============================================================================

static boolean file_source_fill_input_buffer(struct jpeg_decompress_struct *cinfo)
{
  auto *file_buffer = (FileBuffer *)cinfo->client_data;
  size_t nbytes = 0;
  do {
    auto n = read(file_buffer->file_id, file_buffer->buffer + nbytes, file_buffer->buflen - nbytes);
    if (n < 0) {
      break;// error
    }
    if (n == 0) break;// EOF reached...
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
  return static_cast<boolean>(true);
}

//=============================================================================

static void file_source_skip_input_data(struct jpeg_decompress_struct *cinfo, long num_bytes)
{
  if (num_bytes > 0) {
    while (num_bytes > (long)cinfo->src->bytes_in_buffer) {
      num_bytes -= (long)cinfo->src->bytes_in_buffer;
      (void)file_source_fill_input_buffer(cinfo);
    }
  }
  cinfo->src->next_input_byte += (size_t)num_bytes;
  cinfo->src->bytes_in_buffer -= (size_t)num_bytes;
}

//=============================================================================

static void term_file_source(struct jpeg_decompress_struct *cinfo)
{
  auto *file_buffer = (FileBuffer *)cinfo->client_data;

  delete[] file_buffer->buffer;
  delete file_buffer;
  cinfo->client_data = nullptr;
  free(cinfo->src);
  cinfo->src = nullptr;
}

//=============================================================================

/*!
 * Load the JPEG file
 */
static void jpeg_file_src(struct jpeg_decompress_struct *cinfo, int file_id)
{
  struct jpeg_source_mgr *srcmgr;
  FileBuffer *file_buffer;
  cinfo->client_data = new FileBuffer;
  file_buffer = (FileBuffer *)cinfo->client_data;

  file_buffer->buffer = new JOCTET[65536];
  file_buffer->buflen = 65536;
  file_buffer->file_id = file_id;

  srcmgr = (struct jpeg_source_mgr *)malloc(sizeof(struct jpeg_source_mgr));
  srcmgr->init_source = init_file_source;
  srcmgr->fill_input_buffer = file_source_fill_input_buffer;
  srcmgr->skip_input_data = file_source_skip_input_data;
  srcmgr->resync_to_restart = jpeg_resync_to_restart;// default!
  srcmgr->term_source = term_file_source;

  cinfo->src = srcmgr;
}

//=============================================================================


/*!
 * Struct that is used to hold the variables for defining the
 * private I/O routines which are used to write the the HTTP socket
 */
typedef struct HtmlBuffer
{
  JOCTET *buffer;//!< Buffer for holding data to be written out
  size_t buflen;//!< length of the buffer
  shttps::Connection *conobj;//!< Pointer to the connection objects
} HtmlBuffer;

/*!
 * Function which initializes the structures for managing the IO
 */
static void init_html_destination(j_compress_ptr cinfo)
{
  auto *html_buffer = (HtmlBuffer *)cinfo->client_data;
  cinfo->dest->free_in_buffer = html_buffer->buflen;
  cinfo->dest->next_output_byte = html_buffer->buffer;
}

//=============================================================================

/*!
 * Function empty the libjeg buffer and write the data to the socket
 */
static boolean empty_html_buffer(j_compress_ptr cinfo)
{
  auto *html_buffer = (HtmlBuffer *)cinfo->client_data;
  try {
    html_buffer->conobj->sendAndFlush(html_buffer->buffer, html_buffer->buflen);
  } catch (int i) {
    // an error occurred (possibly a broken pipe)
    throw JpegError("Couldn't write to HTTP socket");
    // return false;
  }
  cinfo->dest->free_in_buffer = html_buffer->buflen;
  cinfo->dest->next_output_byte = html_buffer->buffer;

  return static_cast<boolean>(true);
}

//=============================================================================

/*!
 * Finish writing data
 */
static void term_html_destination(j_compress_ptr cinfo)
{
  auto *html_buffer = (HtmlBuffer *)cinfo->client_data;
  size_t nbytes = cinfo->dest->next_output_byte - html_buffer->buffer;
  try {
    html_buffer->conobj->sendAndFlush(html_buffer->buffer, nbytes);
  } catch (int i) {
    // an error occured in sending the data (broken pipe?)
    throw JpegError("Couldn't write to HTTP socket");
  }

  free(html_buffer->buffer);
  free(html_buffer);
  cinfo->client_data = nullptr;

  free(cinfo->dest);
  cinfo->dest = nullptr;
}

//=============================================================================

static void cleanup_html_destination(j_compress_ptr cinfo)
{
  auto *html_buffer = (HtmlBuffer *)cinfo->client_data;
  free(html_buffer->buffer);
  free(html_buffer);
  cinfo->client_data = nullptr;

  free(cinfo->dest);
  cinfo->dest = nullptr;
}

//=============================================================================

/*!
 * This function is used to setup the I/O destination to the HTTP socket
 */
static void jpeg_html_dest(struct jpeg_compress_struct *cinfo, shttps::Connection *conobj)
{
  struct jpeg_destination_mgr *destmgr;
  HtmlBuffer *html_buffer;
  cinfo->client_data = malloc(sizeof(HtmlBuffer));
  html_buffer = (HtmlBuffer *)cinfo->client_data;

  html_buffer->buffer = (JOCTET *)malloc(65536 * sizeof(JOCTET));
  html_buffer->buflen = 65536;
  html_buffer->conobj = conobj;

  destmgr = (struct jpeg_destination_mgr *)malloc(sizeof(struct jpeg_destination_mgr));

  destmgr->init_destination = init_html_destination;
  destmgr->empty_output_buffer = empty_html_buffer;
  destmgr->term_destination = term_html_destination;

  cinfo->dest = destmgr;
}

//=============================================================================

void SipiIOJpeg::parse_photoshop(SipiImage *img, char *data, int length)
{
  int slen;
  unsigned int datalen = 0;
  char *ptr = data;
  unsigned short id;
  char sig[5];
  char name[256];
  int i;

  // cerr << "Parse photoshop: TOTAL LENGTH = " << length << endl;

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
    id = ((unsigned char)*(ptr + 0) << 8) | (unsigned char)*(ptr + 1);// ID
    ptr += 2;// ID

    //
    // name processing (Pascal string)
    slen = *ptr;
    for (i = 0; (i < slen) && (i < 256); i++) name[i] = *(ptr + i + 1);
    name[i] = '\0';
    slen++;// add length byte
    if ((slen % 2) == 1) slen++;
    ptr += slen;

    //
    // data processing
    datalen = ((unsigned char)*ptr << 24) | ((unsigned char)*(ptr + 1) << 16) | ((unsigned char)*(ptr + 2) << 8)
              | (unsigned char)*(ptr + 3);

    ptr += 4;

    switch (id) {
    case 0x0404: {
      // IPTC data
      // cerr << ">>> Photoshop: IPTC" << endl;
      if (img->iptc == nullptr) img->iptc = std::make_shared<SipiIptc>((unsigned char *)ptr, datalen);
      // IPTC – handled separately!
      break;
    }
    case 0x040f: {
      // ICC data
      // cerr << ">>> Photoshop: ICC" << endl;
      // ICC profile
      if (img->icc == nullptr) img->icc = std::make_shared<SipiIcc>((unsigned char *)ptr, datalen);
      break;
    }
    case 0x0422: {
      // EXIF data
      if (img->exif == nullptr) img->exif = std::make_shared<SipiExif>((unsigned char *)ptr, datalen);
      uint16_t ori;
      if (img->exif->getValByKey("Exif.Image.Orientation", ori)) { img->orientation = Orientation(ori); }
      break;
    }
    case 0x0424: {
      // XMP data
      // cerr << ">>> Photoshop: XMP" << endl;
      // XMP data
      if (img->xmp == nullptr) img->xmp = std::make_shared<SipiXmp>(ptr, datalen);
    }
    default: {
      // URL
      char *str = (char *)calloc(1, (datalen + 1) * sizeof(char));
      memcpy(str, ptr, datalen);
      str[datalen] = '\0';
      // fprintf(stderr, "XXX=%s\n", str);
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
static void jpegErrorExit(j_common_ptr cinfo)
{
  char jpegLastErrorMsg[JMSG_LENGTH_MAX];
  /* Create the message */
  (*(cinfo->err->format_message))(cinfo, jpegLastErrorMsg);
  /* Jump to the setjmp point */
  throw JpegError(jpegLastErrorMsg);
}

//=============================================================================


bool SipiIOJpeg::read(SipiImage *img,
  const std::string &filepath,
  std::shared_ptr<SipiRegion> region,
  std::shared_ptr<SipiSize> size,
  bool force_bps_8,
  ScalingQuality scaling_quality)
{
  int infile;
  //
  // open the input file
  //
  if ((infile = ::open(filepath.c_str(), O_RDONLY)) == -1) { return false; }
  // workaround for bug #0011: jpeglib crashes the app when the file is not a jpeg file
  // we check the magic number before calling any jpeglib routines
  unsigned char magic[2];
  if (::read(infile, magic, 2) != 2) { return false; }
  if ((magic[0] != 0xff) || (magic[1] != 0xd8)) {
    close(infile);
    return false;// it's not a JPEG file!
  }
  // move infile position back to the beginning of the file
  ::lseek(infile, 0, SEEK_SET);

  //
  // Since libjpeg is not thread safe, we have unfortunately use a mutex...
  //
  // std::lock_guard<std::mutex> inlock_mutex_guard(inlock);

  struct jpeg_decompress_struct cinfo
  {
  };
  struct jpeg_error_mgr jerr
  {
  };

  JSAMPARRAY linbuf = nullptr;
  jpeg_saved_marker_ptr marker = nullptr;

  //
  // let's create the decompressor
  //
  jpeg_create_decompress(&cinfo);

  cinfo.dct_method = JDCT_FLOAT;

  cinfo.err = jpeg_std_error(&jerr);
  jerr.error_exit = jpegErrorExit;

  try {
    // jpeg_stdio_src(&cinfo, infile);
    jpeg_file_src(&cinfo, infile);
    jpeg_save_markers(&cinfo, JPEG_COM, 0xffff);
    for (int i = 0; i < 16; i++) { jpeg_save_markers(&cinfo, JPEG_APP0 + i, 0xffff); }
  } catch (JpegError &jpgerr) {
    jpeg_destroy_decompress(&cinfo);
    close(infile);
    throw SipiError("Error reading JPEG file: \"" + filepath + "\": " + jpgerr.what());
  }

  //
  // now we read the header
  //
  int res;
  try {
    res = jpeg_read_header(&cinfo, static_cast<boolean>(true));
  } catch (JpegError &jpgerr) {
    jpeg_destroy_decompress(&cinfo);
    close(infile);
    throw SipiImageError("Error reading JPEG file: \"" + filepath + "\": " + jpgerr.what());
  }
  if (res != JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&cinfo);
    close(infile);
    throw SipiImageError("Error reading JPEG file: \"" + filepath + "\"");
  }

  auto no_cropping = static_cast<boolean>(false);
  if (region == nullptr) { no_cropping = static_cast<boolean>(true); }
  if ((region != nullptr) && (region->getType()) == SipiRegion::FULL) { no_cropping = static_cast<boolean>(true); }

  size_t nnx, nny;
  SipiSize::SizeType rtype = SipiSize::FULL;
  if (size != nullptr) { rtype = size->getType(); }

  if (no_cropping == 1) {
    //
    // here we prepare tha scaling/reduce stuff...
    //
    int reduce = 3;// maximal reduce factor is 3: 1/1, 1/2, 1/4 and 1/8
    if ((size != nullptr) && (rtype != SipiSize::FULL)) {
      bool redonly = true;// we assume that only a reduce is necessary
      size->get_size(cinfo.image_width, cinfo.image_height, nnx, nny, reduce, redonly);
    } else {
      reduce = 0;
    }

    reduce = std::max(reduce, 0);
    cinfo.scale_num = 1;
    cinfo.scale_denom = 1;
    for (int i = 0; i < reduce; i++) { cinfo.scale_denom *= 2; }
  }
  cinfo.do_fancy_upsampling = static_cast<boolean>(false);

  //
  // set default orientation
  //
  img->orientation = TOPLEFT;

  //
  // getting Metadata
  //
  marker = cinfo.marker_list;
  unsigned char *icc_buffer = nullptr;
  int icc_buffer_len = 0;
  while (marker != nullptr) {
    if (marker->marker == JPEG_COM) {
      std::string emdatastr(reinterpret_cast<char *>(marker->data), marker->data_length);
      if (emdatastr.compare(0, 5, "SIPI:", 5) == 0) {
        SipiEssentials se(emdatastr);
        img->essential_metadata(se);
      }
    } else if (marker->marker == JPEG_APP0 + 1) {
      // EXIF, XMP MARKER....
      //
      // first we try to find the exif part
      //
      auto *pos = static_cast<unsigned char *>(memmem(marker->data, marker->data_length, "Exif\000\000", 6));
      if (pos != nullptr) {
        img->exif = std::make_shared<SipiExif>(pos + 6, marker->data_length - (pos - marker->data) - 6);
        uint16_t ori;
        if (img->exif->getValByKey("Exif.Image.Orientation", ori)) { img->orientation = static_cast<Orientation>(ori); }
      }

      //
      // first we try to find the xmp part: TODO: reading XMP which spans multiple segments. See ExtendedXMP !!!
      //
      pos = (unsigned char *)memmem(marker->data, marker->data_length, "http://ns.adobe.com/xap/1.0/\000", 29);
      if (pos != nullptr) {
        try {
          char start[] = { '<', '?', 'x', 'p', 'a', 'c', 'k', 'e', 't', ' ', 'b', 'e', 'g', 'i', 'n', '\0' };
          char end[] = { '<', '?', 'x', 'p', 'a', 'c', 'k', 'e', 't', ' ', 'e', 'n', 'd', '\0' };

          char *s;
          unsigned int ll = 0;
          do {
            s = start;
            // skip to the start marker
            while ((ll < marker->data_length) && (*pos != *s)) {
              pos++;
              //// ISSUE: code failes here if there are many concurrent access; data overrrun??
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
            throw SipiError("XMP Problem");
          }
          // now we start reading the data
          while ((ll < marker->data_length) && (*pos != '>')) {
            ll++;
            pos++;
          }
          pos++;// finally we have the start of XMP string
          unsigned char *start_xmp = pos;

          unsigned char *end_xmp;
          do {
            s = end;
            while (*pos != *s) pos++;
            end_xmp = pos;// a candidate
            while ((*s != '\0') && (*pos == *s)) {
              pos++;
              s++;
            }
          } while (*s != '\0');
          while (*pos != '>') { pos++; }
          pos++;

          size_t xmp_len = end_xmp - start_xmp;

          std::string xmpstr((char *)start_xmp, xmp_len);
          size_t npos = xmpstr.find("</x:xmpmeta>");
          xmpstr = xmpstr.substr(0, npos + 12);

          img->xmp = std::make_shared<SipiXmp>(xmpstr);
        } catch (SipiError &err) {
          std::cerr << "Failed to parse XMP..." << '\n';
        }
      }
    } else if (marker->marker == JPEG_APP0 + 2) {
      // ICC MARKER.... may span multiple marker segments
      //
      // first we try to find the exif part
      //
      auto *pos = static_cast<unsigned char *>(memmem(marker->data, marker->data_length, "ICC_PROFILE\0", 12));
      if (pos != nullptr) {
        auto len = marker->data_length - (pos - (unsigned char *)marker->data) - 14;
        icc_buffer = static_cast<unsigned char *>(realloc(icc_buffer, icc_buffer_len + len));
        Sipi::memcpy(icc_buffer + icc_buffer_len, pos + 14, (size_t)len);
        icc_buffer_len += len;
      }
    } else if (marker->marker == JPEG_APP0 + 13) {
      // PHOTOSHOP MARKER....
      if (strncmp("Photoshop 3.0", (char *)marker->data, 14) == 0) {
        parse_photoshop(img, (char *)marker->data + 14, (int)marker->data_length - 14);
      }
    } else {
      // fprintf(stderr, "4) MARKER= %d, %d Bytes, ==> %s\n\n", marker->marker - JPEG_APP0, marker->data_length,
      // marker->data);
    }
    marker = marker->next;
  }
  if (icc_buffer != nullptr) { img->icc = std::make_shared<SipiIcc>(icc_buffer, icc_buffer_len); }

  try {
    jpeg_start_decompress(&cinfo);
  } catch (JpegError &jpgerr) {
    jpeg_destroy_decompress(&cinfo);
    close(infile);
    throw SipiImageError("Error reading JPEG file: \"" + filepath + "\": " + jpgerr.what());
  }

  img->bps = 8;
  img->nx = cinfo.output_width;
  img->ny = cinfo.output_height;
  img->nc = cinfo.output_components;
  int colspace = cinfo.out_color_space;
  // JCS_UNKNOWN, JCS_GRAYSCALE, JCS_RGB, JCS_YCbCr, JCS_CMYK, JCS_YCCK
  switch (colspace) {
  case JCS_RGB: {
    img->photo = PhotometricInterpretation::RGB;
    break;
  }
  case JCS_GRAYSCALE: {
    img->photo = PhotometricInterpretation::MINISBLACK;
    break;
  }
  case JCS_CMYK: {
    img->photo = PhotometricInterpretation::SEPARATED;
    break;
  }
  case JCS_YCbCr: {
    img->photo = PhotometricInterpretation::YCBCR;
    break;
  }
  case JCS_YCCK: {
    throw SipiImageError("Unsupported JPEG colorspace (JCS_YCCK)!");
  }
  case JCS_UNKNOWN: {
    throw SipiImageError("Unsupported JPEG colorspace (JCS_UNKNOWN)!");
  }
  default: {
    throw SipiImageError("Unsupported JPEG colorspace!");
  }
  }
  int sll = cinfo.output_components * cinfo.output_width * sizeof(uint8);

  img->pixels = new byte[img->ny * sll];

  try {
    cinfo.err->msg_code = 0;
    linbuf = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, sll, 1);
    for (size_t i = 0; i < img->ny; i++) {
      jpeg_read_scanlines(&cinfo, linbuf, 1);
      memcpy(&(img->pixels[i * sll]), linbuf[0], (size_t)sll);
    }
  } catch (JpegError &jpgerr) {
    jpeg_destroy_decompress(&cinfo);
    close(infile);
    throw SipiImageError("Error reading JPEG file: \"" + filepath + "\": " + jpgerr.what());
  }

  if (cinfo.err->msg_code == JWRN_HIT_MARKER) {
    jpeg_destroy_decompress(&cinfo);
    close(infile);
    throw SipiImageError(
      "Error reading JPEG file: corrupt JPEG data reported by libjpeg, code " + std::to_string(cinfo.err->msg_code));
  }

  try {
    jpeg_finish_decompress(&cinfo);
  } catch (JpegError &jpgerr) {
    close(infile);
    throw SipiImageError("Error reading JPEG file: \"" + filepath + "\": " + jpgerr.what());
  }

  try {
    jpeg_destroy_decompress(&cinfo);
  } catch (JpegError &jpgerr) {
    close(infile);
    // inlock.unlock();
    throw SipiImageError("Error reading JPEG file: \"" + filepath + "\": " + jpgerr.what());
  }
  close(infile);

  //
  // do some cropping...
  //
  if (no_cropping == 0) {
    // not no cropping (!!) means "do crop"!
    //
    // let's first crop the region (we read the full size image in this case)
    //
    (void)img->crop(region);

    //
    // no we scale the region to the desired size
    //
    int reduce = -1;
    bool redonly = false;
    (void)size->get_size(img->nx, img->ny, nnx, nny, reduce, redonly);
  }

  //
  // resize/Scale the image if necessary
  //
  if ((size != nullptr) && (rtype != SipiSize::FULL)) {
    switch (scaling_quality.jpeg) {
    case ScalingMethod::HIGH:
      img->scale(nnx, nny);
      break;
    case ScalingMethod::MEDIUM:
      img->scaleMedium(nnx, nny);
      break;
    case ScalingMethod::LOW:
      img->scaleFast(nnx, nny);
      break;
    }
  }

  return true;
}

//============================================================================


#define readbyte(a, b)                      \
  do                                        \
    if (((a) = getc((b))) == EOF) return 0; \
  while (0)
#define readword(a, b)                                                  \
  do {                                                                  \
    int cc_ = 0, dd_ = 0;                                               \
    if ((cc_ = getc((b))) == EOF || (dd_ = getc((b))) == EOF) return 0; \
    (a) = (cc_ << 8) + (dd_);                                           \
  } while (0)


SipiImgInfo SipiIOJpeg::getDim(const std::string &filepath)
{
  int infile;
  SipiImgInfo info;
  //
  // open the input file
  //
  if ((infile = ::open(filepath.c_str(), O_RDONLY)) == -1) {
    info.success = SipiImgInfo::FAILURE;
    return info;
  }
  // workaround for bug #0011: jpeglib crashes the app when the file is not a jpeg file
  // we check the magic number before calling any jpeglib routines
  unsigned char magic[2];
  if (::read(infile, magic, 2) != 2) {
    ::close(infile);
    info.success = SipiImgInfo::FAILURE;
    return info;
  }
  if ((magic[0] != 0xff) || (magic[1] != 0xd8)) {
    ::close(infile);
    info.success = SipiImgInfo::FAILURE;
    return info;
  }

  // move infile position back to the beginning of the file
  ::lseek(infile, 0, SEEK_SET);

  struct jpeg_decompress_struct cinfo
  {
  };
  struct jpeg_error_mgr jerr
  {
  };

  jpeg_saved_marker_ptr marker;

  jpeg_create_decompress(&cinfo);

  cinfo.dct_method = JDCT_FLOAT;

  cinfo.err = jpeg_std_error(&jerr);
  jerr.error_exit = jpegErrorExit;

  try {
    jpeg_file_src(&cinfo, infile);
    jpeg_save_markers(&cinfo, JPEG_COM, 0xffff);
    for (int i = 0; i < 16; i++) { jpeg_save_markers(&cinfo, JPEG_APP0 + i, 0xffff); }
  } catch (JpegError &jpgerr) {
    jpeg_destroy_decompress(&cinfo);
    close(infile);
    info.success = SipiImgInfo::FAILURE;
    return info;
  }

  //
  // now we read the header
  //
  int res;
  try {
    res = jpeg_read_header(&cinfo, static_cast<boolean>(true));
  } catch (JpegError &jpgerr) {
    jpeg_destroy_decompress(&cinfo);
    close(infile);
    info.success = SipiImgInfo::FAILURE;
    return info;
  }
  if (res != JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&cinfo);
    close(infile);
    info.success = SipiImgInfo::FAILURE;
    return info;
  }

  SipiImage img{};
  //
  // getting Metadata
  //
  marker = cinfo.marker_list;
  while (marker) {
    if (marker->marker == JPEG_COM) {
      std::string emdatastr((char *)marker->data, marker->data_length);
      if (emdatastr.compare(0, 5, "SIPI:", 5) == 0) {
        SipiEssentials se(emdatastr);
        img.essential_metadata(se);
      }
    } else if (marker->marker == JPEG_APP0 + 1) {
      // EXIF, XMP MARKER....
      //
      // first we try to find the exif part
      //
      auto *pos = (unsigned char *)memmem(marker->data, marker->data_length, "Exif\000\000", 6);
      if (pos != nullptr) {
        img.exif = std::make_shared<SipiExif>(pos + 6, marker->data_length - (pos - marker->data) - 6);
      }

      //
      // first we try to find the xmp part: TODO: reading XMP which spans multiple segments. See ExtendedXMP !!!
      //
      pos = (unsigned char *)memmem(marker->data, marker->data_length, "http://ns.adobe.com/xap/1.0/\000", 29);
      if (pos != nullptr) {
        try {
          char start[] = { '<', '?', 'x', 'p', 'a', 'c', 'k', 'e', 't', ' ', 'b', 'e', 'g', 'i', 'n', '\0' };
          char end[] = { '<', '?', 'x', 'p', 'a', 'c', 'k', 'e', 't', ' ', 'e', 'n', 'd', '\0' };

          char *s;
          unsigned int ll = 0;
          do {
            s = start;
            // skip to the start marker
            while ((ll < marker->data_length) && (*pos != *s)) {
              pos++;
              //// ISSUE: code fails here if there are many concurrent access; data overrrun??
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
            break;// XMP empty
          }
          // now we start reading the data
          while ((ll < marker->data_length) && (*pos != '>')) {
            ll++;
            pos++;
          }
          pos++;// finally we have the start of XMP string
          unsigned char *start_xmp = pos;

          unsigned char *end_xmp;
          do {
            s = end;
            while (*pos != *s) pos++;
            end_xmp = pos;// a candidate
            while ((*s != '\0') && (*pos == *s)) {
              pos++;
              s++;
            }
          } while (*s != '\0');
          while (*pos != '>') { pos++; }
          pos++;

          size_t xmp_len = end_xmp - start_xmp;

          std::string xmpstr((char *)start_xmp, xmp_len);
          size_t npos = xmpstr.find("</x:xmpmeta>");
          xmpstr = xmpstr.substr(0, npos + 12);

          img.xmp = std::make_shared<SipiXmp>(xmpstr);
        } catch (SipiImageError &err) {
          info.success = SipiImgInfo::FAILURE;
          return info;
        }
      }
    } else if (marker->marker == JPEG_APP0 + 13) {
      // PHOTOSHOP MARKER....
      if (strncmp("Photoshop 3.0", (char *)marker->data, 14) == 0) {
        parse_photoshop(&img, (char *)marker->data + 14, (int)marker->data_length - 14);
      }
    }
    marker = marker->next;
  }

  try {
    jpeg_start_decompress(&cinfo);
  } catch (JpegError &jpgerr) {
    jpeg_destroy_decompress(&cinfo);
    close(infile);
    info.success = SipiImgInfo::FAILURE;
    return info;
  }

  info.width = cinfo.output_width;
  info.height = cinfo.output_height;
  info.orientation = TOPLEFT;
  if (img.exif != nullptr) {
    uint16_t ori;
    if (img.exif->getValByKey("Exif.Image.Orientation", ori)) { info.orientation = Orientation(ori); }
  }
  info.success = SipiImgInfo::DIMS;
  jpeg_destroy_decompress(&cinfo);
  close(infile);
  return info;// portions derived from IJG code */
}

//============================================================================


void SipiIOJpeg::write(SipiImage *img, const std::string &filepath, const SipiCompressionParams *params)
{
  int quality = 80;
  if ((params != nullptr) && (!params->empty())) {
    try {
      quality = stoi(params->at(JPEG_QUALITY));
    } catch (const std::out_of_range &er) {
      throw SipiImageError("JPEG quality argument must be integer between 0 and 100");
    } catch (const std::invalid_argument &ia) {
      throw SipiImageError("JPEG quality argument must be integer between 0 and 100");
    }
    if ((quality < 0) || (quality > 100)) {
      throw SipiImageError("JPEG quality argument must be integer between 0 and 100");
    }
  }

  //
  // we have to check if the image has an alpha channel (not supported by JPEG). If
  // so, we remove it!
  //
  const int number_of_alpha_channels = img->getNalpha();
  const int number_of_channels = img->getNc();
  bool three_or_more_channels = number_of_channels > 3;
  bool more_than_zero_alpha_channel = number_of_alpha_channels > 0;

  bool range_valid = three_or_more_channels && more_than_zero_alpha_channel;
  if (range_valid) {
    // we can have an alpha channel and possibly a CMYK image
    img->removeExtraSamples(false);
  }

  auto icc = Sipi::SipiIcc(Sipi::icc_sRGB);// force sRGB !!
  img->convertToIcc(icc, 8);// only 8 bit JPEGs are supported by the spec

  jpeg_compress_struct cinfo{};
  jpeg_error_mgr jerr{};

  cinfo.err = jpeg_std_error(&jerr);
  jerr.error_exit = jpegErrorExit;

  int outfile = -1; /* target file */
  JSAMPROW row_pointer[1]; /* pointer to JSAMPLE row[s] */
  int row_stride; /* physical row width in image buffer */

  try {
    jpeg_create_compress(&cinfo);
  } catch (JpegError &jpgerr) {
    throw SipiImageError(jpgerr.what());
  }

  if (filepath == "HTTP") {
    // we are transmitting the data through the webserver
    shttps::Connection *conobj = img->connection();
    try {
      jpeg_html_dest(&cinfo, conobj);
    } catch (JpegError &jpgerr) {
      cleanup_html_destination(&cinfo);
      jpeg_destroy_compress(&cinfo);
      throw SipiImageError(jpgerr.what());
    }
  } else {
    if (filepath == "stdout:") {
      jpeg_stdio_dest(&cinfo, stdout);
    } else {
      if ((outfile = open(filepath.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1) {
        jpeg_destroy_compress(&cinfo);
        throw SipiImageError("Cannot open file \"" + filepath + "\"!");
      }
      jpeg_file_dest(&cinfo, outfile);
    }
  }

  cinfo.image_width = (int)img->nx; /* image width and height, in pixels */
  cinfo.image_height = (int)img->ny;
  cinfo.input_components = (int)img->nc; /* # of color components per pixel */
  switch (img->photo) {
  case PhotometricInterpretation::MINISWHITE:
  case PhotometricInterpretation::MINISBLACK: {
    if (img->nc != 1) {
      jpeg_destroy_compress(&cinfo);
      throw SipiImageError("Num of components not 1 (nc = " + std::to_string(img->nc) + ")!");
    }
    cinfo.in_color_space = JCS_GRAYSCALE;
    cinfo.jpeg_color_space = JCS_GRAYSCALE;
    break;
  }
  case PhotometricInterpretation::RGB: {
    if (img->nc != 3) {
      jpeg_destroy_compress(&cinfo);
      throw SipiImageError("Num of components not 3 (nc = " + std::to_string(img->nc) + ")!");
    }
    cinfo.in_color_space = JCS_RGB;
    cinfo.jpeg_color_space = JCS_RGB;
    break;
  }
  case PhotometricInterpretation::SEPARATED: {
    if (img->nc != 4) {
      jpeg_destroy_compress(&cinfo);
      throw SipiImageError("Num of components not 3 (nc = " + std::to_string(img->nc) + ")!");
    }
    cinfo.in_color_space = JCS_CMYK;
    cinfo.jpeg_color_space = JCS_CMYK;
    break;
  }
  case PhotometricInterpretation::YCBCR: {
    if (img->nc != 3) {
      jpeg_destroy_compress(&cinfo);
      throw SipiImageError("Num of components not 3 (nc = " + std::to_string(img->nc) + ")!");
    }
    cinfo.in_color_space = JCS_YCbCr;
    cinfo.jpeg_color_space = JCS_YCbCr;
    break;
  }
  case PhotometricInterpretation::CIELAB: {
    img->convertToIcc(SipiIcc(Sipi::PredefinedProfiles::icc_sRGB), 8);
    cinfo.in_color_space = JCS_RGB;
    cinfo.jpeg_color_space = JCS_RGB;
    break;
  }
  default: {
    jpeg_destroy_compress(&cinfo);
    throw SipiImageError("Unsupported JPEG colorspace: " + to_string(img->photo));
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
    // outlock.unlock();
    throw SipiImageError(jpgerr.what());
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
      size_t start_l = sizeof(start) - 1;// remove trailing '\0';
      auto exifchunk = shttps::make_unique<unsigned char[]>(buf.size() + start_l);
      Sipi::memcpy(exifchunk.get(), start, (size_t)start_l);
      Sipi::memcpy(exifchunk.get() + start_l, buf.data(), (size_t)buf.size());

      try {
        jpeg_write_marker(&cinfo, JPEG_APP0 + 1, (JOCTET *)exifchunk.get(), start_l + buf.size());
      } catch (JpegError &jpgerr) {
        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);
        if (outfile != -1) close(outfile);
        throw SipiImageError(jpgerr.what());
      }
    } else {
      // std::cerr << "exif to big" << std::endl;
    }
  }

  if (img->xmp != nullptr) {
    std::string buf = img->xmp->xmpBytes();

    if ((!buf.empty()) && (buf.size() <= 65535)) {
      char start[] = "http://ns.adobe.com/xap/1.0/\000";
      size_t start_l = sizeof(start) - 1;// remove trailing '\0';
      auto xmpchunk = shttps::make_unique<char[]>(buf.size() + start_l);
      Sipi::memcpy(xmpchunk.get(), start, (size_t)start_l);
      Sipi::memcpy(xmpchunk.get() + start_l, buf.data(), (size_t)buf.size());
      try {
        jpeg_write_marker(&cinfo, JPEG_APP0 + 1, (JOCTET *)xmpchunk.get(), start_l + buf.size());
      } catch (JpegError &jpgerr) {
        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);
        if (outfile != -1) close(outfile);
        throw SipiImageError(jpgerr.what());
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
    unsigned char start[14] = {
      0x49, 0x43, 0x43, 0x5F, 0x50, 0x52, 0x4F, 0x46, 0x49, 0x4C, 0x45, 0x0
    };//"ICC_PROFILE\000";
    size_t start_l = 14;
    unsigned int n = buf.size() / (65533 - start_l + 1) + 1;

    auto iccchunk = shttps::make_unique<unsigned char[]>(65533);

    unsigned int n_towrite = buf.size();
    unsigned int n_nextwrite = 65533 - start_l;
    unsigned int n_written = 0;
    for (unsigned int i = 0; i < n; i++) {
      start[12] = (unsigned char)(i + 1);
      start[13] = (unsigned char)n;
      if (n_nextwrite > n_towrite) n_nextwrite = n_towrite;
      Sipi::memcpy(iccchunk.get(), start, (size_t)start_l);
      Sipi::memcpy(iccchunk.get() + start_l, buf.data() + n_written, (size_t)n_nextwrite);
      try {
        jpeg_write_marker(&cinfo, ICC_MARKER, iccchunk.get(), n_nextwrite + start_l);
      } catch (JpegError &jpgerr) {
        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);
        if (outfile != -1) close(outfile);
        throw SipiImageError(jpgerr.what());
      }

      n_towrite -= n_nextwrite;
      n_written += n_nextwrite;
    }
    if (n_towrite != 0) { std::cerr << "Hoppla!" << '\n'; }
  }

  if (img->iptc != nullptr) {
    std::vector<unsigned char> buf = img->iptc->iptcBytes();
    if (buf.size() <= 65535) {
      char start[] = " Photoshop 3.0\0008BIM\004\004\000\000";
      size_t start_l = sizeof(start) - 1;
      unsigned char siz[4];
      siz[0] = (unsigned char)((buf.size() >> 24) & 0x000000ff);
      siz[1] = (unsigned char)((buf.size() >> 16) & 0x000000ff);
      siz[2] = (unsigned char)((buf.size() >> 8) & 0x000000ff);
      siz[3] = (unsigned char)(buf.size() & 0x000000ff);

      auto iptcchunk = shttps::make_unique<char[]>(start_l + 4 + buf.size());
      Sipi::memcpy(iptcchunk.get(), start, (size_t)start_l);
      Sipi::memcpy(iptcchunk.get() + start_l, siz, (size_t)4);
      Sipi::memcpy(iptcchunk.get() + start_l + 4, buf.data(), (size_t)buf.size());

      try {
        jpeg_write_marker(&cinfo, JPEG_APP0 + 13, (JOCTET *)iptcchunk.get(), start_l + buf.size());
      } catch (JpegError &jpgerr) {
        jpeg_destroy_compress(&cinfo);
        if (outfile != -1) close(outfile);
        throw SipiImageError(jpgerr.what());
      }
    } else {
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
      jpeg_write_marker(&cinfo, JPEG_COM, (JOCTET *)sipi_buf, len);
    } catch (JpegError &jpgerr) {
      jpeg_destroy_compress(&cinfo);
      if (outfile != -1) close(outfile);
      throw SipiImageError(jpgerr.what());
    }
  }

  row_stride = img->nx * img->nc; /* JSAMPLEs per row in image_buffer */

  try {
    while (cinfo.next_scanline < cinfo.image_height) {
      // jpeg_write_scanlines expects an array of pointers to scanlines.
      // Here the array is only one element long, but you could pass
      // more than one scanline at a time if that's more convenient.
      row_pointer[0] = &img->pixels[cinfo.next_scanline * row_stride];
      (void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
  } catch (JpegError &jpgerr) {
    jpeg_destroy_compress(&cinfo);
    if (outfile != -1) close(outfile);
    throw SipiImageError(jpgerr.what());
  }

  try {
    jpeg_finish_compress(&cinfo);
  } catch (JpegError &jpgerr) {
    jpeg_destroy_compress(&cinfo);
    if (outfile != -1) close(outfile);
    throw SipiImageError(jpgerr.what());
  }
  if (outfile != -1) close(outfile);

  jpeg_destroy_compress(&cinfo);
}
}// namespace Sipi
