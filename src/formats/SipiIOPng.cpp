/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <arpa/inet.h>
#include <cassert>
#include <csetjmp>
#include <cstdint>
#include <cstdlib>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <cstring>

#include <png.h>
#include <tiff.h>
#include <zlib.h>

#include "logging/logger.h"
#include "SipiImageError.h"
#include "formats/SipiIOPng.h"
#include "observability/profiling.h"

// bad hack in order to include definitions in png.h on debian systems
#if !defined(PNG_TEXT_SUPPORTED)
#define PNG_TEXT_SUPPORTED 1
#endif
#if !defined(PNG_iTXt_SUPPORTED)
#define PNG_iTXt_SUPPORTED 1
#endif

#define PNG_BYTES_TO_CHECK 4

namespace Sipi {

static char lang_en[] = "en";
static char exif_tag[] = "Raw profile type exif";
static char iptc_tag[] = "Raw profile type iptc";
static char xmp_tag[] = "XML:com.adobe.xmp";
static char sipi_tag[] = "SIPI:io.sipi.essentials";

//============== HELPER CLASS ==================
// NOTE: pointers returned by next() are invalidated by the following next()
// call; write the entry through them immediately (as add_zTXt/add_iTXt do).
class PngTextPtr
{
private:
  std::vector<png_text> text;

public:
  inline PngTextPtr(unsigned int len = 16) { text.reserve(len); };

  inline unsigned int num() const { return static_cast<unsigned int>(text.size()); };

  inline png_text *ptr() { return text.data(); };

  png_text *next();

  void add_zTXt(char *key, char *data, unsigned int len);

  void add_iTXt(char *key, char *data, unsigned int len);
};

png_text *PngTextPtr::next() { return &text.emplace_back(); }
//=============================================

void PngTextPtr::add_zTXt(char *key, char *data, unsigned int len)
{
  png_text *tmp = this->next();
  tmp->compression = PNG_TEXT_COMPRESSION_zTXt;
  tmp->key = key;
  tmp->text = (char *)data;
  tmp->text_length = len;
  tmp->itxt_length = 0;
  tmp->lang = (char *)"";
  tmp->lang_key = (char *)"";
}
//=============================================

void PngTextPtr::add_iTXt(char *key, char *data, unsigned int len)
{
  png_text *tmp = this->next();
  tmp->compression = PNG_ITXT_COMPRESSION_zTXt;
  tmp->key = key;
  tmp->text = data;
  tmp->text_length = 0;
  tmp->itxt_length = len;
  tmp->lang = (char *)"";
  tmp->lang_key = (char *)"";
}
//=============================================

void sipi_error_fn(png_structp png_ptr, png_const_charp error_msg)
{
  log_err("PNG error: %s", error_msg);
  // Use longjmp via png_jmpbuf — the canonical libpng error handling pattern.
  // Throwing C++ exceptions through libpng's C stack frames is undefined behavior.
  longjmp(png_jmpbuf(png_ptr), 1);
}

void sipi_warning_fn(png_structp png_ptr, png_const_charp warning_msg)
{
  log_warn("PNG warning: %s", warning_msg);
}

bool SipiIOPng::read(SipiImage *img,
  const std::string &filepath,
  std::shared_ptr<SipiRegion> region,
  std::shared_ptr<SipiSize> size,
  bool force_bps_8,
  ScalingQuality scaling_quality)
{
  SIPI_ZONE_N("SipiIOPng::read");
  unsigned char header[PNG_BYTES_TO_CHECK];
  png_structp png_ptr;
  png_infop info_ptr;

  //
  // open the input file. The unique_ptr is declared before the setjmp, so its
  // destructor runs on the C++ unwind path when the handler below throws.
  //
  auto infile = std::unique_ptr<FILE, decltype(&fclose)>(fopen(filepath.c_str(), "rb"), fclose);
  if (infile == nullptr) { return FALSE; }

  //
  // check header if we really have a PNG file...
  //
  fread(header, 1, PNG_BYTES_TO_CHECK, infile.get());
  if (png_sig_cmp(header, 0, PNG_BYTES_TO_CHECK) != 0) {
    return FALSE;// it's not a PNG file
  }

  if ((png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp) nullptr, sipi_error_fn, sipi_warning_fn))
      == nullptr) {
    throw SipiImageError("Error reading PNG file \"" + filepath + "\": Could not allocate memory for png_structp !");
  }
  if ((info_ptr = png_create_info_struct(png_ptr)) == nullptr) {
    png_destroy_read_struct(&png_ptr, nullptr, nullptr);
    throw SipiImageError("Error reading PNG file \"" + filepath + "\": Could not allocate memory for png_infop !");
  }

  // Declared before the setjmp: on longjmp the handler throws, and the C++
  // unwind path destroys these (they are resized inside the window, which is
  // safe — the vector object's storage is stable memory, not a register).
  std::vector<uint8_t> buffer;
  std::vector<png_bytep> row_pointers;

  // setjmp error recovery — sipi_error_fn calls longjmp(png_jmpbuf(png_ptr), 1)
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    throw SipiImageError("PNG read failed for \"" + filepath + "\"");
  }

  png_init_io(png_ptr, infile.get());
  png_set_sig_bytes(png_ptr, PNG_BYTES_TO_CHECK);
  png_read_info(png_ptr, info_ptr);

  png_uint_32 width, height;
  int color_type, bit_depth, interlace_type;
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, NULL, NULL);
  img->nx = width;
  img->ny = height;
  img->orientation = TOPLEFT;

  png_set_packing(png_ptr);

  png_uint_32 res_x, res_y;
  int unit_type;
  if (png_get_pHYs(png_ptr, info_ptr, &res_x, &res_y, &unit_type)) {
    img->exif = std::make_shared<Exif>();
    float fres_x, fres_y;
    if (unit_type == PNG_RESOLUTION_METER) {
      fres_x = res_x / 39.37007874015748;
      fres_y = res_y / 39.37007874015748;
    } else {
      fres_x = res_x;
      fres_y = res_y;
    }
    img->exif->addKeyVal("Exif.Image.XResolution", Exif::toRational(fres_x));
    img->exif->addKeyVal("Exif.Image.YResolution", Exif::toRational(fres_y));
    img->exif->addKeyVal("Exif.Image.ResolutionUnit", 2);// DPI
  }

  switch (color_type) {
  case PNG_COLOR_TYPE_GRAY: {// implies nc = 1, (bit depths 1, 2, 4, 8, 16)
    png_set_expand_gray_1_2_4_to_8(png_ptr);
    img->photo = PhotometricInterpretation::MINISBLACK;
    break;
  }
  case PNG_COLOR_TYPE_GRAY_ALPHA: {// implies nc = 2, (bit depths 8, 16)
    png_set_expand_gray_1_2_4_to_8(png_ptr);
    img->photo = PhotometricInterpretation::MINISBLACK;
    img->es.push_back(ExtraSamples::ASSOCALPHA);
    break;
  }
  case PNG_COLOR_TYPE_PALETTE: {// might have an alpha channel – we check further below...
    png_set_palette_to_rgb(png_ptr);
    img->photo = PhotometricInterpretation::RGB;
    break;
  }
  case PNG_COLOR_TYPE_RGB: {// implies nc = 3 (standard case :-), (bit_depths 8, 16)
    img->photo = PhotometricInterpretation::RGB;
    break;
  }
  case PNG_COLOR_TYPE_RGBA: {// implies nc = 4, (bit_depths 8, 16)
    img->photo = PhotometricInterpretation::RGB;
    img->es.push_back(ExtraSamples::ASSOCALPHA);
    break;
  }
  }
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS) != 0) png_set_tRNS_to_alpha(png_ptr);

  png_color_16 *image_background;
  if (png_get_bKGD(png_ptr, info_ptr, &image_background) != 0) {
    png_set_background(png_ptr, image_background, PNG_BACKGROUND_GAMMA_FILE, 1, 1.0);
  }

  png_read_update_info(png_ptr, info_ptr);

  //
  // check for ICC profiles...
  //
  int srgb_intent;
  if (png_get_sRGB(png_ptr, info_ptr, &srgb_intent) != 0) {
    img->icc = std::make_shared<Icc>(icc_sRGB);
  } else {
    png_charp name;
    int compression_type = PNG_COMPRESSION_TYPE_BASE;
    png_bytep profile;
    png_uint_32 proflen;
    if (png_get_iCCP(png_ptr, info_ptr, &name, &compression_type, &profile, &proflen) != 0) {
      img->icc = std::make_shared<Icc>((unsigned char *)profile, (int)proflen);
    }
  }
  png_text *png_texts;
  int num_comments = png_get_text(png_ptr, info_ptr, &png_texts, nullptr);

  for (int i = 0; i < num_comments; i++) {
    if (strcmp(png_texts[i].key, xmp_tag) == 0) {
      img->xmp = std::make_shared<Xmp>((char *)png_texts[i].text, (int)png_texts[i].text_length);
    } else if (strcmp(png_texts[i].key, exif_tag) == 0) {
      try {
        img->exif =
          std::make_shared<Exif>((unsigned char *)png_texts[i].text, (unsigned int)png_texts[i].text_length);
      } catch (SipiError &err) {
        // TODO: better error handling – now we nothing at all
      }
    } else if (strcmp(png_texts[i].key, iptc_tag) == 0) {
      img->iptc =
        std::make_shared<Iptc>((unsigned char *)png_texts[i].text, (unsigned int)png_texts[i].text_length);
    } else if (strcmp(png_texts[i].key, sipi_tag) == 0) {
      Essentials se = Essentials::parse_legacy(png_texts[i].text);
      img->essential_metadata(se);
    } else {
      fprintf(stderr, "PNG-COMMENT: key=\"%s\" text=\"%s\"\n", png_texts[i].key, png_texts[i].text);
    }
  }

  size_t sll = png_get_rowbytes(png_ptr, info_ptr);
  buffer.resize(height * sll);

  row_pointers.resize(height);
  for (size_t i = 0; i < img->ny; i++) { row_pointers[i] = (buffer.data() + i * sll); }

  png_read_image(png_ptr, row_pointers.data());
  png_read_end(png_ptr, info_ptr);
  img->bps = png_get_bit_depth(png_ptr, info_ptr);
  img->nc = png_get_channels(png_ptr, info_ptr);
  if (color_type == PNG_COLOR_TYPE_PALETTE && img->nc == 4) { img->es.push_back(ExtraSamples::ASSOCALPHA); }

  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

  if (img->bps == 16) {
    auto *tmp = (unsigned short *)buffer.data();
    for (int i = 0; i < img->nx * img->ny * img->nc; i++) { tmp[i] = ntohs(tmp[i]); }
  }
  img->pixels = std::move(buffer);

  infile.reset();

  if (region != nullptr) {// we just use the image.crop method
    (void)img->crop(region);
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
      case ScalingMethod::HIGH:
        img->scale(nnx, nny);
        break;
      case ScalingMethod::MEDIUM:
        img->scaleMedium(nnx, nny);
        break;
      case ScalingMethod::LOW:
        img->scaleFast(nnx, nny);
      }
    }
  }

  if (force_bps_8) { img->to8bps(); }
  return true;
};

/*==========================================================================*/


SipiImgInfo SipiIOPng::read_shape(const std::string &filepath)
{
  SIPI_ZONE_N("SipiIOPng::read_shape");
  SipiImgInfo info;
  unsigned char header[8];

  //
  // open the input file
  //
  auto infile = std::unique_ptr<FILE, decltype(&fclose)>(fopen(filepath.c_str(), "rb"), fclose);
  if (!infile) {
    info.success = SipiImgInfo::FAILURE;
    return info;
  }
  fread(header, 1, 8, infile.get());
  if (png_sig_cmp(header, 0, 8) != 0) {
    info.success = SipiImgInfo::FAILURE;
    return info;
  }

  png_structp png_ptr;
  png_infop info_ptr;

  if ((png_ptr = png_create_read_struct(
         PNG_LIBPNG_VER_STRING, (png_voidp) nullptr, (png_error_ptr)sipi_error_fn, (png_error_ptr)sipi_warning_fn))
      == nullptr) {
    throw SipiImageError("Error reading PNG file \"" + filepath + "\": Could not allocate memory for png_structp !");
  }
  if ((info_ptr = png_create_info_struct(png_ptr)) == nullptr) {
    png_destroy_read_struct(&png_ptr, nullptr, nullptr);
    throw SipiImageError("Error reading PNG file \"" + filepath + "\": Could not allocate memory for png_infop !");
  }

  // setjmp error recovery for read_shape
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    info.success = SipiImgInfo::FAILURE;
    return info;
  }

  png_init_io(png_ptr, infile.get());
  png_set_sig_bytes(png_ptr, 8);
  png_read_info(png_ptr, info_ptr);

  info.width = png_get_image_width(png_ptr, info_ptr);
  info.height = png_get_image_height(png_ptr, info_ptr);
  info.nc = png_get_channels(png_ptr, info_ptr);
  info.bps = png_get_bit_depth(png_ptr, info_ptr);
  info.orientation = TOPLEFT;
  info.success = SipiImgInfo::DIMS;

  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

  return info;
}
/*==========================================================================*/


void create_text_chunk(PngTextPtr *png_textptr, char *key, char *str, unsigned int len)
{
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

/*!
 * Context passed as the libpng I/O pointer for HTTP writes.
 * The `client_aborted` flag is set when a send/flush fails because the
 * peer closed the socket; the top-level setjmp handler reads it to
 * distinguish client aborts from genuine write errors.
 */
struct PngHttpCtx
{
  SinkStream *sink;
  bool client_aborted;
};

static void conn_write_data(png_structp png_ptr, png_bytep data, png_size_t length)
{
  auto *ctx = static_cast<PngHttpCtx *>(png_get_io_ptr(png_ptr));

  // A non-zero sink return means the response stream (the HTTP socket) is no
  // longer usable. No C++ exception crosses libpng's C frames — the sink
  // callback returns a status code, which we route to libpng's error path.
  if (ctx->sink->write(data, length) != 0) {
    ctx->client_aborted = true;
    png_error(png_ptr, "HTTP write failed");// → sipi_error_fn → longjmp
  }
}

/*==========================================================================*/

static void conn_flush_data(png_structp /*png_ptr*/)
{
  // No-op: the per-chunk SinkStream write already pushed bytes downstream, and
  // the HTTP framing/terminator is flushed by the request handler after write()
  // returns (SipiHttpServer serve_iiif). libpng requires a non-null flush fn.
}

/*==========================================================================*/

void SipiIOPng::write(SipiImage *img, const OutputSink &sink, const SipiCompressionParams *params)
{
  SIPI_ZONE_N("SipiIOPng::write");
  // A streamed sink (callback/tee) is driven through SinkStream via libpng's
  // write callback; a FilePath uses libpng's native file/stdout writer.
  const bool streaming = is_streaming_sink(sink);
  const std::string filepath = streaming ? std::string("<http response>") : std::get<FilePath>(sink).path;

  FILE *outfile = nullptr;
  png_structp png_ptr;

  // Owns the output FILE for the file branch (stdout is never owned).
  // Declared before the setjmp so the handler's throw unwinds it.
  auto outfile_guard = std::unique_ptr<FILE, decltype(&fclose)>(nullptr, fclose);

  if (!(png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, sipi_error_fn, sipi_warning_fn))) {
    throw SipiImageError("Error writing PNG file \"" + filepath + "\": png_create_write_struct failed !");
  }

  // Streamed-write context: SinkStream and http_ctx are kept alive for the
  // whole of SipiIOPng::write so their addresses stay valid across longjmp.
  // For a FilePath we leave them unused and let libpng's native file writer run.
  std::unique_ptr<SinkStream> sink_stream;
  PngHttpCtx http_ctx{ nullptr, false };

  if (streaming) {
    sink_stream = std::make_unique<SinkStream>(sink);
    http_ctx.sink = sink_stream.get();
    png_set_write_fn(png_ptr, &http_ctx, conn_write_data, conn_flush_data);
  } else if (filepath == "stdout:") {
    outfile = stdout;
  } else {
    if (!(outfile = fopen(filepath.c_str(), "wb"))) {
      png_free_data(png_ptr, nullptr, PNG_FREE_ALL, -1);
      throw SipiImageError("Error writing PNG file \"" + filepath + "\": Could not open output file!");
    }
    outfile_guard.reset(outfile);
  }

  png_infop info_ptr;
  if (!(info_ptr = png_create_info_struct(png_ptr))) {
    png_free_data(png_ptr, nullptr, PNG_FREE_ALL, -1);
    throw SipiImageError("Error writing PNG file \"" + filepath + "\": png_create_info_struct !");
  }

  // setjmp error recovery for write — sipi_error_fn calls longjmp
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_write_struct(&png_ptr, &info_ptr);
    if (http_ctx.client_aborted) {
      throw SipiImageClientAbortError("Client aborted HTTP response during PNG write");
    }
    throw SipiImageError("PNG write failed for \"" + filepath + "\"");
  }

  if (outfile != nullptr) png_init_io(png_ptr, outfile);

  png_set_filter(png_ptr, 0, PNG_FILTER_NONE);

  /* set the zlib compression level */
  png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);


  // PNG does not support alpha channels, so we have to remove them if they are present
  if ((img->getNc() > 3) && (img->getNalpha() > 0)) {// we have an alpha channel and possibly a CMYK image
    img->removeExtraSamples();
  }

  int color_type;
  if (img->nc == 1) {// grey value
    color_type = PNG_COLOR_TYPE_GRAY;
  } else if ((img->nc == 2) && (img->es.size() == 1)) {// grey value with alpha
    color_type = PNG_COLOR_TYPE_GRAY_ALPHA;
  } else if (img->nc == 3) {// RGB
    color_type = PNG_COLOR_TYPE_RGB;
  } else if ((img->nc == 4) && (img->es.size() == 1)) {// RGB + ALPHA
    color_type = PNG_COLOR_TYPE_RGB_ALPHA;
  } else if (img->nc == 4) {
    img->convertToIcc(Icc(Sipi::PredefinedProfiles::icc_sRGB), 8);
    color_type = PNG_COLOR_TYPE_RGB;
    img->nc = 3;
    img->bps = 8;
  } else {
    png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
    throw SipiImageError("Error writing PNG file \"" + filepath + "\": unsupported number of channels (" + std::to_string(img->nc) + "), expected 1, 2, 3, or 4");
  }

  png_set_IHDR(png_ptr,
    info_ptr,
    img->nx,
    img->ny,
    img->bps,
    color_type,
    PNG_INTERLACE_NONE,
    PNG_COMPRESSION_TYPE_DEFAULT,
    PNG_FILTER_TYPE_DEFAULT);

  //
  // ICC profile handfling is special...
  //
  Essentials es = img->essential_metadata();
  if ((img->icc != nullptr) || es.fields().use_icc) {
    if ((img->icc != nullptr) && (img->icc->getProfileType() == icc_LAB)) {
      img->convertToIcc(Icc(Sipi::PredefinedProfiles::icc_sRGB), img->bps);
    }
    std::vector<unsigned char> icc_buf;
    try {
      if (es.fields().use_icc) {
        icc_buf = es.fields().icc_profile;
      } else {
        icc_buf = img->icc->iccBytes();
      }
      png_set_iCCP(png_ptr, info_ptr, "ICC", PNG_COMPRESSION_TYPE_BASE, icc_buf.data(), icc_buf.size());
    } catch (SipiError &err) {
      log_err("Error writing ICC profile in PNG: %s", err.what());
    }
  }

  PngTextPtr chunk_ptr(4);

  //
  // other metadata comes here
  //

  std::vector<unsigned char> exif_buf;
  if (img->exif) {
    exif_buf = img->exif->exifBytes();
    chunk_ptr.add_zTXt(exif_tag, (char *)exif_buf.data(), exif_buf.size());
  }

  std::vector<unsigned char> iptc_buf;
  if (img->iptc) {
    iptc_buf = img->iptc->iptcBytes();
    chunk_ptr.add_zTXt(iptc_tag, (char *)iptc_buf.data(), iptc_buf.size());
  }

  std::string xmp_buf;
  if (img->xmp != nullptr) {
    xmp_buf = img->xmp->xmpBytes();
    chunk_ptr.add_iTXt(xmp_tag, (char *)xmp_buf.data(), xmp_buf.size());
  }

  // PNG is an Access File format per ADR-0009 — it MUST NOT carry the
  // Essentials packet. The legacy `Essentials es = img->essential_metadata()`
  // declaration above (line 548) still feeds the ICC fallback branch
  // (lines 549-564) but the iTXt SIPI-chunk emission has been removed
  // (DEV-6379).

  if (chunk_ptr.num() > 0) { png_set_text(png_ptr, info_ptr, chunk_ptr.ptr(), chunk_ptr.num()); }

  png_bytep *row_pointers = (png_bytep *)png_malloc(png_ptr, img->ny * sizeof(png_byte *));

  if (img->bps == 8) {
    for (size_t i = 0; i < img->ny; i++) { row_pointers[i] = (img->pixels.data() + i * img->nx * img->nc); }
  } else if (img->bps == 16) {
    for (size_t i = 0; i < img->ny; i++) { row_pointers[i] = (img->pixels.data() + 2 * i * img->nx * img->nc); }
  }

  png_set_rows(png_ptr, info_ptr, row_pointers);

  png_write_info(png_ptr, info_ptr);
  png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_SWAP_ENDIAN,
    nullptr);// we expect the data to be little endian...
  png_write_end(png_ptr, info_ptr);

  png_free(png_ptr, row_pointers);
  row_pointers = nullptr;

  png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
  png_destroy_write_struct(&png_ptr, &info_ptr);

  png_ptr = nullptr;
  info_ptr = nullptr;
}

}
