/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <algorithm>
#include <csetjmp>
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

#include "Logger.h"
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

/// RAII guard for POSIX file descriptors (int fd).
struct FdGuard {
  int fd{-1};
  explicit FdGuard(int fd) : fd(fd) {}
  ~FdGuard() { if (fd >= 0) ::close(fd); }
  FdGuard(const FdGuard &) = delete;
  FdGuard &operator=(const FdGuard &) = delete;
  void release() { fd = -1; }
};

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
/// Extended jpeg_error_mgr with setjmp buffer for safe error handling through
/// libjpeg's C stack frames. This is the canonical IJG libjpeg error handling
/// pattern — the struct's first field must be jpeg_error_mgr so libjpeg can
/// cast cinfo->err to it, and we cast back to access error_jmp.
struct JpegErrorMgr {
  jpeg_error_mgr pub;  // must be first — libjpeg casts to this
  jmp_buf error_jmp;
  char error_message[JMSG_LENGTH_MAX]{};  // char[], not std::string — safe across longjmp
};

/// Error exit for all JPEG paths (read, getDim, write): longjmp back to the
/// setjmp point. This avoids throwing C++ exceptions through libjpeg's C frames.
static void jpegErrorExit(j_common_ptr cinfo)
{
  auto *myerr = reinterpret_cast<JpegErrorMgr *>(cinfo->err);
  (*(cinfo->err->format_message))(cinfo, myerr->error_message);
  longjmp(myerr->error_jmp, 1);
}

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
      ERREXIT(cinfo, JERR_FILE_WRITE);  // triggers jpegErrorExit → longjmp
      return FALSE;  // unreachable, but satisfies return type
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
      ERREXIT(cinfo, JERR_FILE_WRITE);  // triggers jpegErrorExit → longjmp
      return;  // unreachable
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
  } catch (const std::exception &e) {
    log_err("JPEG HTTP write failed: %s", e.what());
    ERREXIT(cinfo, JERR_FILE_WRITE);  // triggers jpegErrorExit → longjmp
    return FALSE;  // unreachable
  } catch (...) {
    log_err("JPEG HTTP write failed: unknown error");
    ERREXIT(cinfo, JERR_FILE_WRITE);
    return FALSE;  // unreachable
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
  } catch (const std::exception &e) {
    log_err("JPEG HTTP write (term) failed: %s", e.what());
    ERREXIT(cinfo, JERR_FILE_WRITE);
    return;  // unreachable
  } catch (...) {
    log_err("JPEG HTTP write (term) failed: unknown error");
    ERREXIT(cinfo, JERR_FILE_WRITE);
    return;  // unreachable
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
  char *end = data + length;
  unsigned short id;
  char name[256];

  while ((ptr - data) < length) {
    // Bounds check: need at least 4 bytes for signature
    if (ptr + 4 > end) break;

    if (memcmp(ptr, "8BIM", 4) != 0) break;
    ptr += 4;

    // Bounds check: need at least 2 bytes for tag ID
    if (ptr + 2 > end) break;
    id = ((unsigned char)*(ptr + 0) << 8) | (unsigned char)*(ptr + 1);
    ptr += 2;

    // Name processing (Pascal string) — bounds check
    if (ptr >= end) break;
    slen = (unsigned char)*ptr;
    if (ptr + 1 + slen > end) break;
    int name_len = (slen < 255) ? slen : 255;
    for (int i = 0; i < name_len; i++) name[i] = *(ptr + i + 1);
    name[name_len] = '\0';
    slen++;// add length byte
    if ((slen % 2) == 1) slen++;
    ptr += slen;

    // Bounds check: need 4 bytes for data length
    if (ptr + 4 > end) break;
    datalen = ((unsigned char)*ptr << 24) | ((unsigned char)*(ptr + 1) << 16) | ((unsigned char)*(ptr + 2) << 8)
              | (unsigned char)*(ptr + 3);
    ptr += 4;

    // Bounds check: validate datalen against remaining buffer
    if (ptr + datalen > end) break;

    switch (id) {
    case 0x0404: {
      if (img->iptc == nullptr) img->iptc = std::make_shared<SipiIptc>((unsigned char *)ptr, datalen);
      break;
    }
    case 0x040f: {
      if (img->icc == nullptr) img->icc = std::make_shared<SipiIcc>((unsigned char *)ptr, datalen);
      break;
    }
    case 0x0422: {
      if (img->exif == nullptr) img->exif = std::make_shared<SipiExif>((unsigned char *)ptr, datalen);
      uint16_t ori;
      if (img->exif->getValByKey("Exif.Image.Orientation", ori)) { img->orientation = Orientation(ori); }
      break;
    }
    case 0x0424: {
      if (img->xmp == nullptr) img->xmp = std::make_shared<SipiXmp>(ptr, datalen);
      break;  // Fix: was missing, causing fall-through to default
    }
    default: {
      // Unknown resource — skip (removed leaking calloc/memcpy)
      break;
    }
    }

    if ((datalen % 2) == 1) datalen++;
    ptr += datalen;
  }
}

//=============================================================================


//=============================================================================

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
  if (::read(infile, magic, 2) != 2) { ::close(infile); return false; }
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

  struct jpeg_decompress_struct cinfo {};
  JpegErrorMgr jerr;

  JSAMPARRAY linbuf = nullptr;
  jpeg_saved_marker_ptr marker = nullptr;
  unsigned char *icc_buffer_guard = nullptr;  // for cleanup on longjmp

  //
  // let's create the decompressor
  //
  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = jpegErrorExit;

  jpeg_create_decompress(&cinfo);
  cinfo.dct_method = JDCT_FLOAT;

  //
  // setjmp error handler — ALL libjpeg errors from this point longjmp here.
  // This replaces the scattered try/catch(JpegError) blocks in the read path,
  // eliminating C++ throw-through-C undefined behavior.
  //
  if (setjmp(jerr.error_jmp)) {
    // longjmp landed here — clean up and throw in C++ context
    free(icc_buffer_guard);  // may have been allocated during marker parsing
    // Call term_source to free the source manager allocated by jpeg_file_src.
    // jpeg_destroy_decompress does NOT call term_source on error paths.
    if (cinfo.src && cinfo.src->term_source) {
      cinfo.src->term_source(&cinfo);
    }
    jpeg_destroy_decompress(&cinfo);
    ::close(infile);
    throw SipiImageError("JPEG read failed for \"" + filepath + "\": " + std::string(jerr.error_message));
  }

  jpeg_file_src(&cinfo, infile);
  jpeg_save_markers(&cinfo, JPEG_COM, 0xffff);
  for (int i = 0; i < 16; i++) { jpeg_save_markers(&cinfo, JPEG_APP0 + i, 0xffff); }

  //
  // now we read the header
  //
  int res = jpeg_read_header(&cinfo, static_cast<boolean>(true));
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
  // Use icc_buffer_guard (declared before setjmp) so longjmp cleanup can free it
  unsigned char *&icc_buffer = icc_buffer_guard;
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
        // Wrap in try/catch so malformed EXIF from legacy Photoshop files
        // (e.g. APP13-before-APP1 with non-ASCII IPTC) does not abort the
        // whole read. See Phase 5.3 of the image-format-support plan.
        try {
          img->exif = std::make_shared<SipiExif>(pos + 6, marker->data_length - (pos - marker->data) - 6);
          uint16_t ori;
          if (img->exif->getValByKey("Exif.Image.Orientation", ori)) { img->orientation = static_cast<Orientation>(ori); }
        } catch (const std::exception &err) {
          log_warn("Failed to parse EXIF metadata from JPEG: %s", err.what());
        }
      }

      //
      // XMP packet: per Adobe XMP Specification Part 3 §1.1.3, the APP1 XMP
      // segment payload starts with the 29-byte namespace header
      // "http://ns.adobe.com/xap/1.0/\0" followed by the raw XMP packet
      // bytes (typically beginning with `<?xpacket begin` or directly with
      // `<x:xmpmeta`). Older Photoshop versions (e.g., CS 2008) omit the
      // optional `<?xpacket>` wrappers entirely, which the previous
      // hand-rolled boundary scanner did not tolerate. Since SipiXmp stores
      // the raw string and does not currently parse it (see SipiXmp.cpp),
      // the simplest correct extraction is "everything after the namespace
      // header is the XMP packet".
      //
      // TODO(DEV-6261): handle ExtendedXMP (multi-APP1-segment XMP packets
      // larger than 64 KB). The "http://ns.adobe.com/xmp/extension/\0"
      // namespace is currently ignored.
      constexpr size_t kXmpNsLen = 29;// "http://ns.adobe.com/xap/1.0/" + NUL
      pos = (unsigned char *)memmem(marker->data, marker->data_length, "http://ns.adobe.com/xap/1.0/\000", kXmpNsLen);
      if (pos != nullptr) {
        try {
          const auto *data_end = (const unsigned char *)marker->data + marker->data_length;
          const unsigned char *xmp_start = pos + kXmpNsLen;
          if (xmp_start < data_end) {
            const size_t xmp_len = data_end - xmp_start;
            img->xmp = std::make_shared<SipiXmp>(std::string((const char *)xmp_start, xmp_len));
          }
        } catch (const std::exception &err) {
          log_warn("Failed to parse XMP metadata from JPEG: %s", err.what());
        }
      }
    } else if (marker->marker == JPEG_APP0 + 2) {
      // ICC MARKER.... may span multiple marker segments
      auto *pos = static_cast<unsigned char *>(memmem(marker->data, marker->data_length, "ICC_PROFILE\0", 12));
      if (pos != nullptr) {
        auto len = marker->data_length - (pos - (unsigned char *)marker->data) - 14;
        auto *newbuf = static_cast<unsigned char *>(realloc(icc_buffer, icc_buffer_len + len));
        if (newbuf == nullptr) {
          // realloc failed — skip this ICC segment, keep what we have
          log_err("realloc failed for ICC buffer (%d bytes)", icc_buffer_len + len);
          break;
        }
        icc_buffer = newbuf;
        memcpy(icc_buffer + icc_buffer_len, pos + 14, (size_t)len);
        icc_buffer_len += len;
      }
    } else if (marker->marker == JPEG_APP0 + 13) {
      // PHOTOSHOP MARKER....
      // Wrapped in try/catch so a malformed IPTC / EXIF / XMP inside the
      // Photoshop resource block does not prevent the image from being read.
      // See Phase 5.3 of the image-format-support plan.
      // TODO(SipiReport-style-guide): refactor SipiIptc / SipiExif / SipiXmp
      // constructors to return std::expected<T, E> and delete this try/catch.
      if (strncmp("Photoshop 3.0", (char *)marker->data, 14) == 0) {
        try {
          parse_photoshop(img, (char *)marker->data + 14, (int)marker->data_length - 14);
        } catch (const std::exception &err) {
          // SipiImageError and SipiError both derive from std::exception, so
          // a single handler covers every fallible parse path below
          // parse_photoshop. Downgrading to log_warn keeps the image read
          // alive when one resource block is malformed.
          log_warn("Failed to parse Photoshop APP13 resource block: %s", err.what());
        }
      }
    } else if (marker->marker == JPEG_APP0 + 14) {
      // Adobe APP14 marker — 12-byte segment payload (data_length excludes
      // the 2-byte marker and 2-byte length field that precede it):
      //   bytes 0-4  : "Adobe" identifier (NO trailing NUL)
      //   bytes 5-6  : version
      //   bytes 7-8  : flags0
      //   bytes 9-10 : flags1
      //   byte  11   : transform flag (0=Unknown/CMYK, 1=YCbCr, 2=YCCK)
      if (marker->data_length >= 12 && memcmp(marker->data, "Adobe", 5) == 0) {
        img->app14_transform = static_cast<uint8_t>(marker->data[11]);
      }
    } else {
      // fprintf(stderr, "4) MARKER= %d, %d Bytes, ==> %s\n\n", marker->marker - JPEG_APP0, marker->data_length,
      // marker->data);
    }
    marker = marker->next;
  }
  if (icc_buffer != nullptr) {
    img->icc = std::make_shared<SipiIcc>(icc_buffer, icc_buffer_len);
    free(icc_buffer);// SipiIcc constructor copies the data
    icc_buffer = nullptr;  // prevent double-free if longjmp fires later
  }

  // icc_buffer is freed and nulled above; errors → longjmp → setjmp handler
  jpeg_start_decompress(&cinfo);

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
    // libjpeg-turbo decodes YCCK internally to CMYK; the post-read
    // inversion handling in Phase 5.2 is shared with the CMYK path.
    img->photo = PhotometricInterpretation::SEPARATED;
    break;
  }
  case JCS_UNKNOWN: {
    throw SipiImageError("Unsupported JPEG colorspace JCS_UNKNOWN in file \"" + filepath
      + "\" (dimensions: " + std::to_string(img->nx) + "x" + std::to_string(img->ny)
      + ", components: " + std::to_string(cinfo.output_components) + ")");
  }
  default: {
    throw SipiImageError("Unsupported JPEG colorspace (code: " + std::to_string(colspace) + ") in file \"" + filepath
      + "\" (dimensions: " + std::to_string(img->nx) + "x" + std::to_string(img->ny)
      + ", components: " + std::to_string(cinfo.output_components) + ")");
  }
  }
  int sll = cinfo.output_components * cinfo.output_width * sizeof(uint8);

  delete[] img->pixels;// free previous buffer if re-reading into same SipiImage
  img->pixels = new byte[img->ny * sll];

  // All libjpeg calls below — errors → longjmp → setjmp handler above
  linbuf = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, sll, 1);
  for (size_t i = 0; i < img->ny; i++) {
    jpeg_read_scanlines(&cinfo, linbuf, 1);
    memcpy(&(img->pixels[i * sll]), linbuf[0], (size_t)sll);
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  ::close(infile);

  //
  // CMYK / YCCK polarity handling (DEV-6257, Phase 5.2).
  //
  // libjpeg-turbo's CMYK output is inverted when the source declares an
  // Adobe APP14 marker with transform=0 (Photoshop-style "Unknown / CMYK")
  // or transform=2 (YCCK, which libjpeg-turbo converts to inverted CMYK
  // internally). In those cases we re-invert (`v = 255 - v`) so the
  // subsequent ICC conversion sees CMYK in the expected polarity.
  //
  // We deliberately do NOT invert when:
  //   - app14_transform == 1 (YCbCr → RGB, already correct polarity)
  //   - app14_transform == 255 (no APP14 marker — raw CMYK as on disk;
  //     inverting here would corrupt files that do not need it; the R10
  //     `JpegCmykRawNoApp14NotInverted` test pins this branch).
  //
  const bool is_cmyk_path = img->photo == PhotometricInterpretation::SEPARATED && img->nc == 4;
  const bool needs_inversion =
    is_cmyk_path && (img->app14_transform == 0 || img->app14_transform == 2);
  if (needs_inversion) {
    const size_t total_bytes = static_cast<size_t>(img->ny) * static_cast<size_t>(sll);
    for (size_t b = 0; b < total_bytes; ++b) { img->pixels[b] = static_cast<byte>(255 - img->pixels[b]); }
  }

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
  SipiImgInfo info;
  //
  // open the input file
  //
  int raw_fd = ::open(filepath.c_str(), O_RDONLY);
  if (raw_fd == -1) {
    info.success = SipiImgInfo::FAILURE;
    return info;
  }
  FdGuard fd_guard(raw_fd);

  // workaround for bug #0011: jpeglib crashes the app when the file is not a jpeg file
  // we check the magic number before calling any jpeglib routines
  unsigned char magic[2];
  if (::read(raw_fd, magic, 2) != 2) {
    info.success = SipiImgInfo::FAILURE;
    return info;
  }
  if ((magic[0] != 0xff) || (magic[1] != 0xd8)) {
    info.success = SipiImgInfo::FAILURE;
    return info;
  }

  // move infile position back to the beginning of the file
  ::lseek(raw_fd, 0, SEEK_SET);

  struct jpeg_decompress_struct cinfo {};
  JpegErrorMgr jerr;

  jpeg_saved_marker_ptr marker;

  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = jpegErrorExit;

  jpeg_create_decompress(&cinfo);
  cinfo.dct_method = JDCT_FLOAT;

  // Helper to clean up jpeg resources (source manager + decompress struct).
  auto cleanup_jpeg = [&cinfo]() {
    if (cinfo.src && cinfo.src->term_source) {
      cinfo.src->term_source(&cinfo);
    }
    jpeg_destroy_decompress(&cinfo);
  };

  // setjmp error handler for getDim — libjpeg errors longjmp here
  if (setjmp(jerr.error_jmp)) {
    cleanup_jpeg();
    info.success = SipiImgInfo::FAILURE;
    return info;
  }

  jpeg_file_src(&cinfo, raw_fd);
  jpeg_save_markers(&cinfo, JPEG_COM, 0xffff);
  for (int i = 0; i < 16; i++) { jpeg_save_markers(&cinfo, JPEG_APP0 + i, 0xffff); }

  //
  // now we read the header
  //
  int res = jpeg_read_header(&cinfo, static_cast<boolean>(true));
  if (res != JPEG_HEADER_OK) {
    cleanup_jpeg();
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
          if (ll >= marker->data_length) {
            throw SipiImageError("Failed to parse XMP in getDim: '>' not found");
          }
          pos++;// skip past '>'
          unsigned char *start_xmp = pos;

          unsigned char *data_end = (unsigned char *)marker->data + marker->data_length;
          unsigned char *end_xmp = start_xmp;
          do {
            s = end;
            while (pos < data_end && *pos != *s) pos++;
            if (pos >= data_end) break;
            end_xmp = pos;// a candidate
            while (pos < data_end && (*s != '\0') && (*pos == *s)) {
              pos++;
              s++;
            }
          } while (pos < data_end && *s != '\0');
          if (pos >= data_end || *s != '\0') {
            throw SipiImageError("Failed to parse XMP in getDim: end marker not found");
          }
          while (pos < data_end && *pos != '>') { pos++; }
          if (pos < data_end) pos++;

          size_t xmp_len = end_xmp - start_xmp;

          std::string xmpstr((char *)start_xmp, xmp_len);
          size_t npos = xmpstr.find("</x:xmpmeta>");
          if (npos != std::string::npos) xmpstr = xmpstr.substr(0, npos + 12);

          img.xmp = std::make_shared<SipiXmp>(xmpstr);
        } catch (SipiImageError &err) {
          cleanup_jpeg();
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

  // errors → longjmp → setjmp handler above
  jpeg_start_decompress(&cinfo);

  info.width = cinfo.output_width;
  info.height = cinfo.output_height;
  info.nc = cinfo.num_components;
  info.bps = cinfo.data_precision;
  info.orientation = TOPLEFT;
  if (img.exif != nullptr) {
    uint16_t ori;
    if (img.exif->getValByKey("Exif.Image.Orientation", ori)) { info.orientation = Orientation(ori); }
  }
  info.success = SipiImgInfo::DIMS;
  cleanup_jpeg();
  // fd_guard closes the file descriptor automatically
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
  JpegErrorMgr jerr;

  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = jpegErrorExit;

  // FdGuard for outfile — constructed before setjmp. Its destructor runs
  // during the C++ stack unwinding from the throw SipiImageError after longjmp.
  FdGuard outfile_guard(-1);
  JSAMPROW row_pointer[1];
  int row_stride;
  bool is_http = (filepath == "HTTP");

  jpeg_create_compress(&cinfo);  // errors → longjmp → setjmp handler below

  //
  // setjmp error handler — ALL libjpeg errors from this point longjmp here.
  // This replaces the scattered try/catch(JpegError) blocks.
  //
  // NOTE on RAII: longjmp does NOT call C++ destructors. Between setjmp and
  // longjmp, the following RAII objects may leak on the error path:
  // - exifchunk, xmpchunk, iccchunk, iptcchunk (make_unique, each <65KB)
  // These are freed when the thread handles the next request (stack unwinding
  // from the SipiImageError thrown below). Acceptable small leak on error path.
  //
  if (setjmp(jerr.error_jmp)) {
    // longjmp landed here — clean up and throw in C++ context
    if (is_http) {
      cleanup_html_destination(&cinfo);
    }
    jpeg_destroy_compress(&cinfo);
    // outfile_guard destructor closes fd during throw unwinding
    throw SipiImageError("JPEG write failed: " + std::string(jerr.error_message));
  }

  if (is_http) {
    shttps::Connection *conobj = img->connection();
    jpeg_html_dest(&cinfo, conobj);
  } else {
    if (filepath == "stdout:") {
      jpeg_stdio_dest(&cinfo, stdout);
    } else {
      int outfile = open(filepath.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
      if (outfile == -1) {
        jpeg_destroy_compress(&cinfo);
        throw SipiImageError("Cannot open file \"" + filepath + "\"!");
      }
      outfile_guard.fd = outfile;
      jpeg_file_dest(&cinfo, outfile);
    }
  }

  cinfo.image_width = (int)img->nx;
  cinfo.image_height = (int)img->ny;
  cinfo.input_components = (int)img->nc;
  switch (img->photo) {
  case PhotometricInterpretation::MINISWHITE:
  case PhotometricInterpretation::MINISBLACK: {
    if (img->nc != 1) {
      jpeg_destroy_compress(&cinfo);
      throw SipiImageError("Cannot write JPEG: grayscale (MINISBLACK) requires 1 channel, got "
        + std::to_string(img->nc) + " (dimensions: " + std::to_string(img->nx) + "x"
        + std::to_string(img->ny) + ", bps: " + std::to_string(img->bps) + ")");
    }
    cinfo.in_color_space = JCS_GRAYSCALE;
    cinfo.jpeg_color_space = JCS_GRAYSCALE;
    break;
  }
  case PhotometricInterpretation::RGB: {
    if (img->nc != 3) {
      jpeg_destroy_compress(&cinfo);
      throw SipiImageError("Cannot write JPEG: RGB requires 3 channels, got "
        + std::to_string(img->nc) + " (dimensions: " + std::to_string(img->nx) + "x"
        + std::to_string(img->ny) + ", bps: " + std::to_string(img->bps) + ")");
    }
    cinfo.in_color_space = JCS_RGB;
    cinfo.jpeg_color_space = JCS_RGB;
    break;
  }
  case PhotometricInterpretation::SEPARATED: {
    if (img->nc != 4) {
      jpeg_destroy_compress(&cinfo);
      throw SipiImageError("Cannot write JPEG: CMYK (SEPARATED) requires 4 channels, got "
        + std::to_string(img->nc) + " (dimensions: " + std::to_string(img->nx) + "x"
        + std::to_string(img->ny) + ", bps: " + std::to_string(img->bps) + ")");
    }
    cinfo.in_color_space = JCS_CMYK;
    cinfo.jpeg_color_space = JCS_CMYK;
    break;
  }
  case PhotometricInterpretation::YCBCR: {
    if (img->nc != 3) {
      jpeg_destroy_compress(&cinfo);
      throw SipiImageError("Cannot write JPEG: YCbCr requires 3 channels, got "
        + std::to_string(img->nc) + " (dimensions: " + std::to_string(img->nx) + "x"
        + std::to_string(img->ny) + ", bps: " + std::to_string(img->bps) + ")");
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
    throw SipiImageError("Cannot write JPEG: unsupported colorspace " + to_string(img->photo)
      + " (dimensions: " + std::to_string(img->nx) + "x" + std::to_string(img->ny)
      + ", channels: " + std::to_string(img->nc) + ", bps: " + std::to_string(img->bps) + ")");
  }
  }
  cinfo.progressive_mode = TRUE;
  cinfo.write_Adobe_marker = TRUE;
  cinfo.write_JFIF_header = TRUE;

  // All libjpeg calls below — errors trigger longjmp to the setjmp handler above
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, TRUE);
  jpeg_simple_progression(&cinfo);
  jpeg_start_compress(&cinfo, TRUE);

  //
  // Markers must be written in sequence: APP0, APP1, APP2, ..., APP15
  //

  if (img->exif != nullptr) {
    std::vector<unsigned char> buf = img->exif->exifBytes();
    if (buf.size() <= 65535) {
      char start[] = "Exif\000\000";
      size_t start_l = sizeof(start) - 1;
      // NOTE: make_unique leak on longjmp is acceptable (see comment above setjmp)
      auto exifchunk = shttps::make_unique<unsigned char[]>(buf.size() + start_l);
      memcpy(exifchunk.get(), start, (size_t)start_l);
      if (buf.size() > 0) memcpy(exifchunk.get() + start_l, buf.data(), (size_t)buf.size());
      jpeg_write_marker(&cinfo, JPEG_APP0 + 1, (JOCTET *)exifchunk.get(), start_l + buf.size());
    }
  }

  if (img->xmp != nullptr) {
    std::string buf = img->xmp->xmpBytes();
    if ((!buf.empty()) && (buf.size() <= 65535)) {
      char start[] = "http://ns.adobe.com/xap/1.0/\000";
      size_t start_l = sizeof(start) - 1;
      auto xmpchunk = shttps::make_unique<char[]>(buf.size() + start_l);
      memcpy(xmpchunk.get(), start, (size_t)start_l);
      memcpy(xmpchunk.get() + start_l, buf.data(), (size_t)buf.size());
      jpeg_write_marker(&cinfo, JPEG_APP0 + 1, (JOCTET *)xmpchunk.get(), start_l + buf.size());
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
      log_err("Error writing ICC profile in JPEG: %s", err.what());
    }
    unsigned char start[14] = {
      0x49, 0x43, 0x43, 0x5F, 0x50, 0x52, 0x4F, 0x46, 0x49, 0x4C, 0x45, 0x0
    };
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
      memcpy(iccchunk.get(), start, (size_t)start_l);
      memcpy(iccchunk.get() + start_l, buf.data() + n_written, (size_t)n_nextwrite);
      jpeg_write_marker(&cinfo, ICC_MARKER, iccchunk.get(), n_nextwrite + start_l);
      n_towrite -= n_nextwrite;
      n_written += n_nextwrite;
    }
    if (n_towrite != 0) { log_warn("Incomplete JPEG ICC write: %u bytes remaining", n_towrite); }
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
      memcpy(iptcchunk.get(), start, (size_t)start_l);
      memcpy(iptcchunk.get() + start_l, siz, (size_t)4);
      if (buf.size() > 0) memcpy(iptcchunk.get() + start_l + 4, buf.data(), (size_t)buf.size());
      jpeg_write_marker(&cinfo, JPEG_APP0 + 13, (JOCTET *)iptcchunk.get(), start_l + buf.size());
    }
  }

  if (es.is_set()) {
    std::string esstr = es;
    unsigned int len = esstr.length();
    char sipi_buf[512 + 1];
    strncpy(sipi_buf, esstr.c_str(), 512);
    sipi_buf[512] = '\0';
    jpeg_write_marker(&cinfo, JPEG_COM, (JOCTET *)sipi_buf, len);
  }

  row_stride = img->nx * img->nc;

  while (cinfo.next_scanline < cinfo.image_height) {
    row_pointer[0] = &img->pixels[cinfo.next_scanline * row_stride];
    (void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  // outfile_guard destructor closes fd
}
}// namespace Sipi
