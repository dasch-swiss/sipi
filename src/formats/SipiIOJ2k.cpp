/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <assert.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <syslog.h>
#include <vector>

#include <cstring>
#include <fcntl.h>

// Kakadu core includes
#include "kdu_compressed.h"
#include "kdu_elementary.h"
#include "kdu_messaging.h"
#include "kdu_params.h"
#include "kdu_sample_processing.h"

// Application level includes

#include "jp2.h"
#include "jpx.h"
#include "kdu_file_io.h"
#include "kdu_stripe_compressor.h"
#include "kdu_stripe_decompressor.h"

#include "shttps/Connection.h"
#include "shttps/Global.h"
#include "shttps/makeunique.h"

#include "SipiError.hpp"
#include "SipiImageError.hpp"
#include "formats/SipiIOJ2k.h"

using namespace kdu_core;
using namespace kdu_supp;

namespace Sipi {

//=========================================================================
// Here we are implementing a subclass of kdu_core::kdu_compressed_target
// in order to write directly to the HTTP server connection
//
class J2kHttpStream : public kdu_core::kdu_compressed_target
{
private:
  shttps::Connection *conobj;

public:
  J2kHttpStream(shttps::Connection *conobj_p);

  ~J2kHttpStream();

  inline int get_capabilities() { return KDU_TARGET_CAP_SEQUENTIAL; };

  inline bool start_rewrite(kdu_long backtrack) { return false; };

  inline bool end_rewrite() { return false; };

  bool write(const kdu_byte *buf, int num_bytes);

  bool close();

  inline bool prefer_large_writes() { return false; }

  inline void set_target_size(kdu_long num_bytes){};// we just ignore it
};
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// Constructor which takes the HTTP server connection as parameter
//........................................................................
J2kHttpStream::J2kHttpStream(shttps::Connection *conobj_p) : kdu_core::kdu_compressed_target() { conobj = conobj_p; };
//-------------------------------------------------------------------------


//-------------------------------------------------------------------------
// Distructor which cleans up !!!!!!!!!! We still have to determine what has to be cleaned up!!!!!!!!!!
//........................................................................
J2kHttpStream::~J2kHttpStream(){
  // cleanup everything...
};
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// Write the data to the HTTP server connection
//........................................................................
bool J2kHttpStream::write(const kdu_byte *buf, int num_bytes)
{
  try {
    conobj->sendAndFlush(buf, num_bytes);
  } catch (int i) {
    return false;
  }
  return true;
};
//-------------------------------------------------------------------------

bool J2kHttpStream::close()
{
  try {
    conobj->flush();
  } catch (int i) {
    return false;
  }
  return true;
}

static kdu_core::kdu_byte
  xmp_uuid[] = { 0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8, 0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC };
static kdu_core::kdu_byte
  iptc_uuid[] = { 0x33, 0xc7, 0xa4, 0xd2, 0xb8, 0x1d, 0x47, 0x23, 0xa0, 0xba, 0xf1, 0xa3, 0xe0, 0x97, 0xad, 0x38 };
static kdu_core::kdu_byte
  exif_uuid[] = { 'J', 'p', 'g', 'T', 'i', 'f', 'f', 'E', 'x', 'i', 'f', '-', '>', 'J', 'P', '2' };
// static kdu_core::kdu_byte geojp2_uuid[] = {0xB1, 0x4B, 0xF8, 0xBD, 0x08, 0x3D, 0x4B, 0x43, 0xA5, 0xAE, 0x8C, 0xD7,
// 0xD5, 0xA6, 0xCE, 0x03}; static kdu_core::kdu_byte world_uuid[] = {0x96, 0xa9, 0xf1, 0xf1, 0xdc, 0x98, 0x40, 0x2d,
// 0xa7, 0xae, 0xd6, 0x8e, 0x34, 0x45, 0x18, 0x09};


/*!
 * Local class for handling kakadu warnings
 */
class KduSipiWarning : public kdu_core::kdu_message
{
private:
  std::string msg;

public:
  KduSipiWarning() : kdu_message() { msg = "KAKADU-WARNING: "; }

  KduSipiWarning(const char *lead_in) : kdu_message(), msg(lead_in) {}

  void put_text(const char *str) { msg += str; }

  void flush(bool end_of_message = false)
  {
    if (end_of_message) { syslog(LOG_WARNING, "%s", msg.c_str()); }
  }
};
//=============================================================================

/*!
 * Local class for handling kakadu errors. It overrides the "exit()" call and
 * throws a kdu_exception...
 */
class KduSipiError : public kdu_core::kdu_message
{
private:
  std::string msg;

public:
  KduSipiError() : kdu_message() { msg = "KAKADU-ERROR: "; }

  KduSipiError(const char *lead_in) : kdu_message(), msg(lead_in) {}

  void put_text(const char *str) { msg += str; }

  void flush(bool end_of_message = false)
  {
    if (end_of_message) {
      syslog(LOG_ERR, "%s", msg.c_str());
      throw KDU_ERROR_EXCEPTION;
    }
  }

  void setMsg(const std::string &msg);
};
//=============================================================================

static KduSipiWarning kdu_sipi_warn("Kakadu-library: ");
static KduSipiError kdu_sipi_error("Kakadu-library: ");

static bool is_jpx(const char *fname)
{
  int inf;
  int retval = 0;
  if ((inf = ::open(fname, O_RDONLY)) != -1) {
    char testbuf[48];
    char sig0[] = { '\xff', '\x52' };
    char sig1[] = { '\xff', '\x4f', '\xff', '\x51' };
    char sig2[] = { '\x00', '\x00', '\x00', '\x0C', '\x6A', '\x50', '\x20', '\x20', '\x0D', '\x0A', '\x87', '\x0A' };
    auto n = read(inf, testbuf, 48);
    if ((n >= 47) && (memcmp(sig0, testbuf + 45, 2) == 0)) {
      retval = 1;
    } else if ((n >= 4) && (memcmp(sig1, testbuf, 4) == 0)) {
      retval = 1;
    } else if ((n >= 12) && (memcmp(sig2, testbuf, 12) == 0))
      retval = 1;
  }
  close(inf);
  return retval == 1;
}
//=============================================================================


bool SipiIOJ2k::read(SipiImage *img,
  const std::string &filepath,
  const std::shared_ptr<SipiRegion> region,
  const std::shared_ptr<SipiSize> size,
  bool force_bps_8,
  ScalingQuality scaling_quality)
{
  if (!is_jpx(filepath.c_str())) return false;// It's not a JPGE2000....

  int num_threads;
  if ((num_threads = kdu_get_num_processors()) < 2) num_threads = 0;

  // Custom messaging services
  kdu_customize_warnings(&kdu_sipi_warn);
  kdu_customize_errors(&kdu_sipi_error);

  kdu_core::kdu_compressed_source *input = nullptr;
  kdu_supp::kdu_simple_file_source file_in;

  kdu_supp::jp2_family_src jp2_ultimate_src;
  kdu_supp::jpx_source jpx_in;
  kdu_supp::jpx_codestream_source jpx_stream;
  kdu_supp::jpx_layer_source jpx_layer;

  kdu_supp::jp2_channels channels;
  kdu_supp::jp2_palette palette;
  kdu_supp::jp2_resolution resolution;
  kdu_supp::jp2_colour colour;

  jp2_ultimate_src.open(filepath.c_str());

  if (jpx_in.open(&jp2_ultimate_src, true)
      < 0) {// if < 0, not compatible with JP2 or JPX.  Try opening as a raw code-stream.
    jp2_ultimate_src.close();
    file_in.open(filepath.c_str());
    input = &file_in;
  } else {
    jp2_input_box box;
    if (box.open(&jp2_ultimate_src)) {
      do {
        if (box.get_box_type() == jp2_uuid_4cc) {
          kdu_byte buf[16];
          box.read(buf, 16);
          if (memcmp(buf, xmp_uuid, 16) == 0) {
            auto xmp_len = box.get_remaining_bytes();
            auto xmp_buf = shttps::make_unique<char[]>(xmp_len);
            box.read((kdu_byte *)xmp_buf.get(), xmp_len);
            try {
              img->xmp = std::make_shared<SipiXmp>(xmp_buf.get(),
                xmp_len);// ToDo: Problem with thread safety!!!!!!!!!!!!!!
            } catch (SipiError &err) {
              syslog(LOG_ERR, "%s", err.to_string().c_str());
            }
          } else if (memcmp(buf, iptc_uuid, 16) == 0) {
            auto iptc_len = box.get_remaining_bytes();
            auto iptc_buf = shttps::make_unique<unsigned char[]>(iptc_len);
            box.read(iptc_buf.get(), iptc_len);
            try {
              img->iptc = std::make_shared<SipiIptc>(iptc_buf.get(), iptc_len);
            } catch (SipiError &err) {
              syslog(LOG_ERR, "%s", err.to_string().c_str());
            }
          } else if (memcmp(buf, exif_uuid, 16) == 0) {
            auto exif_len = box.get_remaining_bytes();
            auto exif_buf = shttps::make_unique<unsigned char[]>(exif_len);
            box.read(exif_buf.get(), exif_len);
            try {
              img->exif = std::make_shared<SipiExif>(exif_buf.get(), exif_len);
            } catch (SipiError &err) {
              syslog(LOG_ERR, "%s", err.to_string().c_str());
            }
          }
        }
        box.close();
      } while (box.open_next());
    }

    int stream_id = 0;
    jpx_stream = jpx_in.access_codestream(stream_id);
    input = jpx_stream.open_stream();
    palette = jpx_stream.access_palette();
  }

  kdu_core::kdu_codestream codestream;
  codestream.create(input);
  // codestream.set_fussy(); // Set the parsing error tolerance.
  codestream.set_fast();// No errors expected in input

  //
  // get the
  int maximal_reduce = codestream.get_min_dwt_levels();

  //
  // get SipiEssentials (if present) as codestream comment
  //
  kdu_codestream_comment comment = codestream.get_comment();
  while (comment.exists()) {
    const char *cstr = comment.get_text();
    if (strncmp(cstr, "SIPI:", 5) == 0) {
      SipiEssentials se(cstr + 5);
      img->essential_metadata(se);
      break;
    }
    comment = codestream.get_comment(comment);
  }

  //
  // get the size of the full image (without reduce!)
  //
  siz_params *siz = codestream.access_siz();
  int __nx, __ny;
  siz->get(Ssize, 0, 0, __ny);
  siz->get(Ssize, 0, 1, __nx);

  /*
  int __clayers;
  __clayers = codestream.get_min_dwt_levels( );
  std::cerr << "Clayers=" << __clayers << std::endl;
  */

  //
  // is there a region of interest defined ? If yes, get the cropping parameters...
  //
  kdu_core::kdu_dims roi;
  bool do_roi = false;
  if ((region != nullptr) && (region->getType()) != SipiRegion::FULL) {
    try {
      size_t sx, sy;
      region->crop_coords(__nx, __ny, roi.pos.x, roi.pos.y, sx, sy);
      roi.size.x = sx;
      roi.size.y = sy;
      do_roi = true;
    } catch (Sipi::SipiError &err) {
      codestream.destroy();
      input->close();
      jpx_in.close();// Not really necessary here.
      throw err;
    }
  }

  //
  // here we prepare tha scaling/reduce stuff...
  //
  int reduce = maximal_reduce;
  size_t nnx, nny;
  bool redonly = true;// we assume that only a reduce is necessary
  if ((size != nullptr) && (size->getType() != SipiSize::FULL)) {
    if (do_roi) {
      size->get_size(roi.size.x, roi.size.y, nnx, nny, reduce, redonly);
    } else {
      size->get_size(__nx, __ny, nnx, nny, reduce, redonly);
    }
  } else {
    reduce = 0;
  }

  if (reduce < 0) reduce = 0;

  codestream.apply_input_restrictions(0, 0, reduce, 0, do_roi ? &roi : nullptr);


  // Determine number of components to decompress
  kdu_core::kdu_dims dims;
  codestream.get_dims(0, dims);

  img->nx = dims.size.x;
  img->ny = dims.size.y;

  img->bps = codestream.get_bit_depth(0);// bitdepth of zeroth component. Assuming it's valid for all

  img->nc = codestream.get_num_components();// not the same as the number of colors!


  //
  // The following definitions we need in case we get a palette color image!
  //
  byte *rlut = NULL;
  byte *glut = NULL;
  byte *blut = NULL;
  //
  // get ICC-Profile if available
  //
  jpx_layer = jpx_in.access_layer(0);
  img->photo = PhotometricInterpretation::INVALID;// we initialize to an invalid value in order to test later if
                                                  // img->photo has been set
  int numcol;
  if (jpx_layer.exists()) {
    kdu_supp::jp2_colour colinfo = jpx_layer.access_colour(0);
    kdu_supp::jp2_channels chaninfo = jpx_layer.access_channels();
    numcol = chaninfo.get_num_colours();// I assume these are the color channels (1, 3 or 4 in case of CMYK)
    int nluts = palette.get_num_luts();
    if (nluts == 3) {
      int nentries = palette.get_num_entries();
      rlut = new byte[nentries];
      glut = new byte[nentries];
      blut = new byte[nentries];
      float *tmplut = new float[nentries];

      palette.get_lut(0, tmplut);
      for (int i = 0; i < nentries; i++) { rlut[i] = roundf((tmplut[i] + 0.5) * 255.0); }

      palette.get_lut(1, tmplut);
      for (int i = 0; i < nentries; i++) { glut[i] = roundf((tmplut[i] + 0.5) * 255.0); }

      palette.get_lut(2, tmplut);
      for (int i = 0; i < nentries; i++) { blut[i] = roundf((tmplut[i] + 0.5) * 255.0); }
      delete[] tmplut;
    }
    img->orientation = TOPLEFT;
    if (img->nc > numcol) {// we have more components than colors -> alpha channel!
      for (size_t i = 0; i < img->nc - numcol; i++) {// img->nc - numcol: number of alpha channels (?)
        img->es.push_back(ExtraSamples::ASSOCALPHA);
      }
    }
    if (colinfo.exists()) {
      int space = colinfo.get_space();
      switch (space) {
      case kdu_supp::JP2_sRGB_SPACE: {
        img->photo = PhotometricInterpretation::RGB;
        img->icc = std::make_shared<SipiIcc>(icc_sRGB);
        break;
      }
      case kdu_supp::JP2_CMYK_SPACE: {
        img->photo = PhotometricInterpretation::SEPARATED;
        img->icc = std::make_shared<SipiIcc>(icc_CMYK_standard);
        break;
      }
      case kdu_supp::JP2_YCbCr1_SPACE: {
        img->photo = PhotometricInterpretation::YCBCR;
        img->icc = std::make_shared<SipiIcc>(icc_sRGB);
        break;
      }
      case kdu_supp::JP2_YCbCr2_SPACE:
      case kdu_supp::JP2_YCbCr3_SPACE: {
        float whitepoint[] = { 0.3127, 0.3290 };
        float primaries[] = { 0.630, 0.340, 0.310, 0.595, 0.155, 0.070 };
        img->photo = PhotometricInterpretation::YCBCR;
        img->icc = std::make_shared<SipiIcc>(whitepoint, primaries);
        break;
      }
      case kdu_supp::JP2_iccRGB_SPACE: {
        img->photo = PhotometricInterpretation::RGB;
        int icc_len;
        const unsigned char *icc_buf = colinfo.get_icc_profile(&icc_len);
        img->icc = std::make_shared<SipiIcc>(icc_buf, icc_len);
        break;
      }
      case kdu_supp::JP2_iccANY_SPACE: {
        if (numcol == 1) {
          img->photo = PhotometricInterpretation::MINISBLACK;
        } else if (numcol == 3) {
          img->photo = PhotometricInterpretation::RGB;
        } else if (numcol == 4) {
          img->photo = PhotometricInterpretation::SEPARATED;
        } else {
          syslog(LOG_ERR, "Unsupported number of colors: %d", numcol);
          throw SipiImageError("Unsupported number of colors: " + std::to_string(numcol));
        }
        int icc_len;
        const unsigned char *icc_buf = colinfo.get_icc_profile(&icc_len);
        img->icc = std::make_shared<SipiIcc>(icc_buf, icc_len);
        break;
      }
      case kdu_supp::JP2_sLUM_SPACE: {
        img->photo = PhotometricInterpretation::MINISBLACK;
        img->icc = std::make_shared<SipiIcc>(icc_LUM_D65);
        break;
      }
      case kdu_supp::JP2_sYCC_SPACE: {
        img->photo = PhotometricInterpretation::YCBCR;
        img->icc = std::make_shared<SipiIcc>(icc_sRGB);
        break;
      }
      case kdu_supp::JP2_CIELab_SPACE: {
        img->photo = PhotometricInterpretation::CIELAB;
        img->icc = std::make_shared<SipiIcc>(icc_LAB);
        break;
      }
      case 100: {
        img->photo = PhotometricInterpretation::MINISBLACK;
        img->icc = std::make_shared<SipiIcc>(icc_ROMM_GRAY);
        break;
      }

      default: {
        syslog(LOG_ERR, "Unsupported ICC profile: %s", std::to_string(space).c_str());
        throw SipiImageError("Unsupported ICC profile: " + std::to_string(space));
      }
      }
    }
  } else {
    numcol = img->nc;
  }

  if (img->photo == PhotometricInterpretation::INVALID) {
    switch (numcol) {
    case 1: {
      img->photo = PhotometricInterpretation::MINISBLACK;
      break;
    }
    case 3: {
      img->photo = PhotometricInterpretation::RGB;
      break;
    }
    case 4: {
      img->photo = PhotometricInterpretation::SEPARATED;
      break;
    }
    default: {
      throw SipiImageError("No meaningful photometric interpretation possible");
    }
    }// switch(numcol)
  }

  //
  // the following code directly converts a 16-Bit jpx into an 8-bit image.
  // In order to retrieve a 16-Bit image, use kdu_uin16 *buffer and the apropriate signature of the pull_stripe method
  //
  kdu_supp::kdu_stripe_decompressor decompressor;
  decompressor.start(codestream);
  // TODO: check image for number of components and make this dynamic
  int stripe_heights[5] = {
    dims.size.y, dims.size.y, dims.size.y, dims.size.y, dims.size.y
  };// enough for alpha channel (5 components)

  if (force_bps_8) img->bps = 8;// forces kakadu to convert to 8 bit!
  switch (img->bps) {
  case 8: {
    auto *buffer8 = new kdu_core::kdu_byte[static_cast<int>(dims.area()) * img->nc];
    try {
      decompressor.pull_stripe(buffer8, stripe_heights);
    } catch (kdu_exception &exc) {
      codestream.destroy();
      input->close();
      jpx_in.close();// Not really necessary here.
      syslog(LOG_ERR, "Error while decompressing image: %s.", filepath.c_str());
      return false;
    }
    img->pixels = buffer8;
    break;
  }
  case 12: {
    std::vector<char> get_signed(img->nc, 0);// vector<bool> does not work -> special treatment in C++
    auto *buffer16 = new kdu_core::kdu_int16[(int)dims.area() * img->nc];
    try {
      decompressor.pull_stripe(
        buffer16, stripe_heights, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<bool *>(get_signed.data()));
    } catch (kdu_exception &exc) {
      codestream.destroy();
      input->close();
      jpx_in.close();// Not really necessary here.
      syslog(LOG_ERR, "Error while decompressing image: %s.", filepath.c_str());
      return false;
    }
    img->pixels = reinterpret_cast<byte *>(buffer16);
    img->bps = 16;
    break;
  }
  case 16: {
    std::vector<char> get_signed(img->nc, 0);// vector<bool> does not work -> special treatment in C++
    auto *buffer16 = new kdu_core::kdu_int16[(int)dims.area() * img->nc];
    try {
      decompressor.pull_stripe(
        buffer16, stripe_heights, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<bool *>(get_signed.data()));
    } catch (kdu_exception &exc) {
      codestream.destroy();
      input->close();
      jpx_in.close();// Not really necessary here.
      syslog(LOG_ERR, "Error while decompressing image: %s.", filepath.c_str());
      return false;
    }
    img->pixels = reinterpret_cast<byte *>(buffer16);
    break;
  }
  default: {
    decompressor.finish();
    codestream.destroy();
    input->close();
    jpx_in.close();// Not really necessary here.
    syslog(LOG_ERR, "Unsupported number of bits/sample: %ld !", img->bps);
    throw SipiImageError("Unsupported number of bits/sample!");
  }
  }
  decompressor.finish();
  codestream.destroy();
  input->close();
  jpx_in.close();// Not really necessary here.

  if (rlut != NULL) {
    //
    // we have a palette color image...
    //
    byte *tmpbuf = new byte[img->nx * img->ny * numcol];
    for (int y = 0; y < img->ny; ++y) {
      for (int x = 0; x < img->nx; ++x) {
        tmpbuf[3 * (y * img->nx + x) + 0] = rlut[img->pixels[y * img->nx + x]];
        tmpbuf[3 * (y * img->nx + x) + 1] = glut[img->pixels[y * img->nx + x]];
        tmpbuf[3 * (y * img->nx + x) + 2] = blut[img->pixels[y * img->nx + x]];
      }
    }
    delete[] img->pixels;
    img->pixels = tmpbuf;
    img->nc = numcol;
    delete[] rlut;
    delete[] glut;
    delete[] blut;
  }
  if (img->photo == PhotometricInterpretation::YCBCR) {
    img->convertYCC2RGB();
    img->photo = PhotometricInterpretation::RGB;
  }

  if ((size != nullptr) && (!redonly)) {
    switch (scaling_quality.jk2) {
    case ScalingMethod::HIGH:
      img->scale(nnx, nny);
      // std::cerr << "===>HIGH SCALING to nnx=" << nnx << " nny=" << nny << std::endl;
      break;
    case ScalingMethod::MEDIUM:
      img->scaleMedium(nnx, nny);
      // std::cerr << "===>MEDIUM SCALING to nnx=" << nnx << " nny=" << nny << std::endl;
      break;
    case ScalingMethod::LOW:
      img->scaleFast(nnx, nny);
      // std::cerr << "===>FAST SCALING to nnx=" << nnx << " nny=" << nny << std::endl;
      break;
    }
  }
  return true;
}
//=============================================================================


SipiImgInfo SipiIOJ2k::getDim(const std::string &filepath)
{
  SipiImgInfo info;
  if (!is_jpx(filepath.c_str())) {
    info.success = SipiImgInfo::FAILURE;
    return info;
  }

  kdu_customize_warnings(&kdu_sipi_warn);
  kdu_customize_errors(&kdu_sipi_error);

  kdu_supp::jp2_family_src jp2_ultimate_src;
  kdu_supp::jpx_source jpx_in;
  kdu_supp::jpx_codestream_source jpx_stream;
  kdu_core::kdu_compressed_source *input = nullptr;
  kdu_supp::kdu_simple_file_source file_in;

  jp2_ultimate_src.open(filepath.c_str());

  if (jpx_in.open(&jp2_ultimate_src, true)
      < 0) {// if < 0, not compatible with JP2 or JPX.  Try opening as a raw code-stream.
    jp2_ultimate_src.close();
    file_in.open(filepath.c_str());
    input = &file_in;
  } else {
    int stream_id = 0;
    jpx_stream = jpx_in.access_codestream(stream_id);
    input = jpx_stream.open_stream();
  }

  kdu_core::kdu_codestream codestream;
  codestream.create(input);
  codestream.set_fussy();// Set the parsing error tolerance.

  //
  // get the size of the full image (without reduce!)
  //
  siz_params *siz = codestream.access_siz();
  int tmp_height;
  siz->get(Ssize, 0, 0, tmp_height);
  info.height = tmp_height;
  int tmp_width;
  siz->get(Ssize, 0, 1, tmp_width);
  info.width = tmp_width;
  info.success = SipiImgInfo::DIMS;

  int __tnx, __tny;
  siz->get(Stiles, 0, 0, __tny);
  siz->get(Stiles, 0, 1, __tnx);
  info.tile_width = __tnx;
  info.tile_height = __tny;
  info.clevels = codestream.get_min_dwt_levels();

  kdu_codestream_comment comment = codestream.get_comment();
  while (comment.exists()) {
    const char *cstr = comment.get_text();
    if (strncmp(cstr, "SIPI:", 5) == 0) {
      SipiEssentials se(cstr + 5);
      info.origmimetype = se.mimetype();
      info.origname = se.origname();
      info.success = SipiImgInfo::ALL;
      break;
    }
    comment = codestream.get_comment(comment);
  }

  codestream.destroy();
  input->close();
  jpx_in.close();// Not really necessary here.

  return info;
}
//=============================================================================


static void write_xmp_box(kdu_supp::jp2_family_tgt *tgt, const char *xmpstr)
{
  kdu_supp::jp2_output_box out;
  out.open(tgt, jp2_uuid_4cc);
  out.set_target_size(strlen(xmpstr) + sizeof(xmp_uuid));
  out.write(xmp_uuid, 16);
  out.write((kdu_core::kdu_byte *)xmpstr, strlen(xmpstr));
  out.close();
}
//=============================================================================

static void write_iptc_box(kdu_supp::jp2_family_tgt *tgt, kdu_core::kdu_byte *iptc, int iptc_len)
{
  kdu_supp::jp2_output_box out;
  out.open(tgt, jp2_uuid_4cc);
  out.set_target_size(iptc_len + sizeof(iptc_uuid));
  out.write(iptc_uuid, 16);
  out.write((kdu_core::kdu_byte *)iptc, iptc_len);
  out.close();
}
//=============================================================================

static void write_exif_box(kdu_supp::jp2_family_tgt *tgt, kdu_core::kdu_byte *exif, int exif_len)
{
  kdu_supp::jp2_output_box out;
  out.open(tgt, jp2_uuid_4cc);
  out.set_target_size(exif_len + sizeof(exif_uuid));
  out.write(exif_uuid, sizeof(exif_uuid));
  out.write((kdu_byte *)exif, exif_len);// NOT::: skip JPEG marker header 'E', 'x', 'i', 'f', '\0', '\0'..
  out.close();
}
//=============================================================================

static long get_bpp_dims(kdu_codestream &codestream)
{
  int comps = codestream.get_num_components();
  int n, max_width = 0, max_height = 0;
  for (n = 0; n < comps; n++) {
    kdu_dims dims;
    codestream.get_dims(n, dims);
    if (dims.size.x > max_width) { max_width = dims.size.x; }
    if (dims.size.y > max_height) { max_height = dims.size.y; }
  }
  return ((long)max_height) * ((long)max_width);
}

void SipiIOJ2k::write(SipiImage *img, const std::string &filepath, const SipiCompressionParams *params)
{
  kdu_customize_warnings(&kdu_sipi_warn);
  kdu_customize_errors(&kdu_sipi_error);

  int num_threads;

  kdu_membroker membroker;

  if ((num_threads = kdu_get_num_processors()) < 2) num_threads = 0;

  try {
    // Construct code-stream object
    siz_params siz;
    siz.set(Scomponents, 0, 0, (int)img->nc);
    siz.set(Sdims, 0, 0, (int)img->ny);// Height of first image component
    siz.set(Sdims, 0, 1, (int)img->nx);// Width of first image component
    siz.set(Sprecision, 0, 0, (int)img->bps);// Bits per sample (usually 8 or 16)
    siz.set(Ssigned, 0, 0, false);// Image samples are originally unsigned

    //
    // tiling has to be done here. Tile size must be adapted to image dimesions!
    //
    int tw = 0, th = 0;
    const int mindim = img->ny < img->nx ? img->ny : img->nx;
    if ((params != nullptr) && (!params->empty())) {
      if (params->find(J2K_Stiles) != params->end()) {
        int n = std::sscanf(params->at(J2K_Stiles).c_str(), "{%d,%d}", &tw, &th);
        if (n != 2) { throw SipiImageError("Tiling parameter invalid!"); }
        if ((mindim > tw) && (mindim > th)) {
          std::stringstream ss;
          ss << "Stiles=" << params->at(J2K_Stiles);
          siz.parse_string(ss.str().c_str());
        }
      }
    } else {
      if (mindim >= 4096) {
        tw = th = 1024;
      } else if (mindim >= 2048) {
        tw = th = 1024;
      } else if (mindim >= 1024) {
        tw = th = 256;
      } else {
        tw = th = 0;
      }
      if (mindim > 1024) {
        std::stringstream ss;
        ss << "Stiles={" << tw << "," << th << "}";
        siz.parse_string(ss.str().c_str());
      }
    }

    kdu_params *siz_ref = &siz;
    siz_ref->finalize();

    kdu_compressed_target *output = nullptr;

    jp2_family_tgt jp2_ultimate_tgt;

    J2kHttpStream *http = nullptr;
    if (filepath == "HTTP") {
      shttps::Connection *conobj = img->connection();
      http = new J2kHttpStream(conobj);
      jp2_ultimate_tgt.open(http, &membroker);
    } else {
      jp2_ultimate_tgt.open(filepath.c_str(), &membroker);
    }

    jpx_target jpx_out;
    jpx_out.open(&jp2_ultimate_tgt, &membroker);
    jpx_codestream_target jpx_stream = jpx_out.add_codestream();
    jpx_layer_target jpx_layer = jpx_out.add_layer();

    // jp2_palette jp2_family_palette = jpx_stream.access_palette();
    jp2_resolution jp2_family_resolution = jpx_layer.access_resolution();

    output = jpx_stream.access_stream();

    kdu_thread_env env;
    kdu_thread_env *env_ref;

    if (num_threads > 0) {
      env.create();
      for (int nt = 1; nt < num_threads; nt++) {
        if (!env.add_thread()) {
          num_threads = nt;// Unable to create all the threads requested
          break;
        }
      }
      env_ref = &env;
    } else {
      env_ref = nullptr;
    }

    kdu_codestream codestream;
    codestream.create(&siz, output, nullptr, 0, 0, env_ref, &membroker);

    // Set up any specific coding parameters and finalize them.
    int num_clayers;
    std::vector<double> rates;

    //
    // always
    //
    bool is_reversible = true;
    codestream.access_siz()->parse_string("Creversible=yes");

    if ((params != nullptr) && (!params->empty())) {

      if (params->find(J2K_Sprofile) != params->end()) {
        std::stringstream ss;
        ss << "Sprofile=" << params->at(J2K_Sprofile);
        codestream.access_siz()->parse_string(ss.str().c_str());
      } else {
        codestream.access_siz()->parse_string("Sprofile=PART2");
      }

      if (params->find(J2K_Clayers) != params->end()) {
        num_clayers = std::stoi(params->at(J2K_Clayers));
        std::stringstream ss;
        ss << "Clayers=" << params->at(J2K_Clayers);
        codestream.access_siz()->parse_string(ss.str().c_str());
      } else {
        codestream.access_siz()->parse_string("Clayers=8");
        num_clayers = 8;
      }

      if (params->find(J2K_Clevels) != params->end()) {
        std::stringstream ss;
        ss << "Clevels=" << params->at(J2K_Clevels);
        codestream.access_siz()->parse_string(ss.str().c_str());
      } else {
        codestream.access_siz()->parse_string("Clevels=8");// resolution levels
      }

      if (params->find(J2K_Corder) != params->end()) {
        std::stringstream ss;
        ss << "Corder=" << params->at(J2K_Corder);
        codestream.access_siz()->parse_string(ss.str().c_str());
      } else {
        codestream.access_siz()->parse_string("Corder=RPCL");
      }

      if (params->find(J2K_Cprecincts) != params->end()) {
        std::stringstream ss;
        ss << "Cprecincts=" << params->at(J2K_Cprecincts);
        codestream.access_siz()->parse_string(ss.str().c_str());
      } else {
        codestream.access_siz()->parse_string("Cprecincts={256,256},{256,256},{128,128}");
      }

      if (params->find(J2K_Cblk) != params->end()) {
        std::stringstream ss;
        ss << "Cblk=" << params->at(J2K_Cblk);
        codestream.access_siz()->parse_string(ss.str().c_str());
      } else {
        codestream.access_siz()->parse_string("Cblk={64,64}");
      }

      if (params->find(J2K_Cuse_sop) != params->end()) {
        std::stringstream ss;
        ss << "Cuse_sop=" << params->at(J2K_Cuse_sop);
        codestream.access_siz()->parse_string(ss.str().c_str());
      } else {
        codestream.access_siz()->parse_string("Cuse_sop=yes");
      }

      if (params->find(J2K_rates) != params->end()) {
        std::string ratestr = params->at(J2K_rates);
        std::stringstream ss(ratestr);
        double temp;
        while (ss >> temp) rates.push_back(temp);// done! now array={102,330,3133,76531,451,000,12,44412}
      }
    } else {
      codestream.access_siz()->parse_string("Sprofile=PART2");
      if (mindim > 4096) {
        codestream.access_siz()->parse_string("Clayers=8");
        num_clayers = 8;
        codestream.access_siz()->parse_string("Clevels=8");// resolution levels ***
      } else if (mindim > 2048) {
        codestream.access_siz()->parse_string("Clayers=5");
        num_clayers = 5;
        codestream.access_siz()->parse_string("Clevels=5");// resolution levels ***
      } else if (mindim > 1024) {
        codestream.access_siz()->parse_string("Clayers=3");
        num_clayers = 3;
        codestream.access_siz()->parse_string("Clevels=3");// resolution levels ***
      }
      codestream.access_siz()->parse_string("Corder=RPCL");
      codestream.access_siz()->parse_string("Cprecincts={256,256}");
      codestream.access_siz()->parse_string("Cblk={64,64}");
      codestream.access_siz()->parse_string("Cuse_sop=yes");
      codestream.access_siz()->parse_string("Cuse_eph=yes");
    }


    codestream.access_siz()->finalize_all();// Set up coding defaults

    jp2_dimensions jp2_family_dimensions = jpx_stream.access_dimensions();
    jp2_family_dimensions.init(&siz);// initalize dimension box

    //
    // get resolution tag from exif if existent: From kakadu:
    // Sets the vertical resolution in high resolution canvas grid points per metre.
    // If for_display is true, this is the desired display resolution; oherwise, it
    // is the capture resolution. There is no need to explicitly specify either of these
    // resolutions. If no resolution is specified the relevant box will not be written
    // in the JP2/JPX file, except when a non-unity aspect ratio is selected.
    // In the latter case, a desired display resolution box will be created, having a
    // default vertical display resolution of one grid-point per metre.
    //
    if (img->exif != nullptr) {
      float res_x = 72., res_y = 72.;
      int unit = 2;// RESUNIT_INCH
      if (img->exif->getValByKey("Exif.Image.XResolution", res_x)
          && img->exif->getValByKey("Exif.Image.YResolution", res_y)) {
        (void)img->exif->getValByKey("Exif.Image.ResolutionUnit", unit);
        switch (unit) {
        case 1:
          break;// RESUNIT_NONE
        case 2: {// RESUNIT_INCH
          res_x = res_x * 100. / 2.51;
          res_y = res_y * 100. / 2.51;
          break;
        }
        case 3: {// RESUNIT_CENTIMETER
          res_x = res_x * 100.;
          res_y = res_y * 100.;
        }
        }
        jp2_family_resolution.init(res_y / res_x);
        jp2_family_resolution.set_resolution(res_x, false);
      }
    }

    //
    // we need the essential metadata in order to preserve unsupported ICC profiles
    //
    SipiEssentials es = img->essential_metadata();

    jp2_colour jp2_family_colour = jpx_layer.add_colour();
    if (img->icc != nullptr) {
      PredefinedProfiles icc_type = img->icc->getProfileType();
      try {
        switch (icc_type) {
        case icc_undefined: {
          unsigned int icc_len;
          kdu_byte *icc_bytes = (kdu_byte *)img->icc->iccBytes(icc_len);
          jp2_family_colour.init(icc_bytes);
          break;
        }
        case icc_unknown: {
          unsigned int icc_len;
          kdu_byte *icc_bytes = (kdu_byte *)img->icc->iccBytes(icc_len);
          jp2_family_colour.init(icc_bytes);
          break;
        }
        case icc_sRGB: {
          // TODO: this fixes the problem with grayscale JPEGs, but is it breaking anything else?
          if (img->nc - img->es.size() == 1) {
            jp2_family_colour.init(JP2_sLUM_SPACE);
          } else {
            jp2_family_colour.init(JP2_sRGB_SPACE);
          }
          break;
        }
        case icc_AdobeRGB: {
          unsigned int icc_len;
          kdu_byte *icc_bytes = (kdu_byte *)img->icc->iccBytes(icc_len);
          jp2_family_colour.init(icc_bytes);
          break;
        }
        case icc_RGB: {// TODO: DOES NOT WORK AS EXPECTED!!!!! Fallback below
          unsigned int icc_len;
          kdu_byte *icc_bytes = (kdu_byte *)img->icc->iccBytes(icc_len);
          jp2_family_colour.init(icc_bytes);
          break;
        }
        case icc_CMYK_standard: {
          jp2_family_colour.init(JP2_CMYK_SPACE);
          break;
        }
        case icc_GRAY_D50: {
          unsigned int icc_len;
          kdu_byte *icc_bytes = (kdu_byte *)img->icc->iccBytes(icc_len);
          jp2_family_colour.init(icc_bytes);// TODO: DOES NOT WORK AS EXPECTED!!!!! Fallback below
          break;
        }
        case icc_LUM_D65: {
          if (es.is_set()) es.use_icc(true);
          jp2_family_colour.init(JP2_sLUM_SPACE);// TODO: just a fallback
          break;
        }
        case icc_ROMM_GRAY: {
          if (es.is_set()) es.use_icc(true);
          jp2_family_colour.init(JP2_sLUM_SPACE);// TODO: just a fallback
          break;
        }
        case icc_LAB: {
          if (img->bps == 8) {
            jp2_family_colour.init(JP2_CIELab_SPACE, 100, 0, 8, 255, 128, 8, 255, 128, 8);
          } else {
            //
            // very strange parameters – was experimentally found
            //
            jp2_family_colour.init(JP2_CIELab_SPACE, 100, 0, 16, 255, 32767, 16, 255, 32767, 16);
          }
          break;
        };
        default: {
          unsigned int icc_len;
          kdu_byte *icc_bytes = (kdu_byte *)img->icc->iccBytes(icc_len);
          jp2_family_colour.init(icc_bytes);
        }
        }
      } catch (kdu_exception e) {
        if (es.is_set()) es.use_icc(true);
        switch (img->nc - img->es.size()) {
        case 1: {
          jp2_family_colour.init(JP2_sLUM_SPACE);
          break;
        }
        case 3: {
          jp2_family_colour.init(JP2_sRGB_SPACE);
          break;
        }
        case 4: {
          jp2_family_colour.init(JP2_CMYK_SPACE);
          break;
        }
        }
      }
    } else {
      switch (img->nc - img->es.size()) {
      case 1: {
        jp2_family_colour.init(JP2_sLUM_SPACE);
        break;
      }
      case 3: {
        jp2_family_colour.init(JP2_sRGB_SPACE);
        break;
      }
      case 4: {
        jp2_family_colour.init(JP2_CMYK_SPACE);
        break;
      }
      }
    }

    //
    // Custom tag for SipiEssential metadata
    //
    if (es.is_set()) {
      std::string esstr = es;
      std::string emdata = "SIPI:" + esstr;
      kdu_codestream_comment comment = codestream.add_comment();
      comment.put_text(emdata.c_str());
    }

    jp2_channels jp2_family_channels = jpx_layer.access_channels();
    jp2_family_channels.init(img->nc - img->es.size());
    for (int c = 0; c < img->nc - img->es.size(); c++) { jp2_family_channels.set_colour_mapping(c, c); }
    if (img->es.size() > 0) {
      if (img->es.size() == 1) {
        for (int c = 0; c < img->nc - img->es.size(); c++) {
          jp2_family_channels.set_opacity_mapping(c, img->nc - img->es.size());
        }
      } else if (img->es.size() == (img->nc - img->es.size())) {
        for (int c = 0; c < img->nc - img->es.size(); c++) {
          jp2_family_channels.set_opacity_mapping(c, img->nc - img->es.size() + c);
        }
      }
    }

    jpx_out.write_headers();
    if (img->iptc != nullptr) {
      std::vector<unsigned char> iptc_buf = img->iptc->iptcBytes();
      write_iptc_box(&jp2_ultimate_tgt, iptc_buf.data(), iptc_buf.size());
    }

    //
    // write EXIF here
    //
    if (img->exif != nullptr) {
      std::vector<unsigned char> exif_buf = img->exif->exifBytes();
      write_exif_box(&jp2_ultimate_tgt, exif_buf.data(), exif_buf.size());
    }

    //
    // write XMP data here
    //
    if (img->xmp != nullptr) {
      std::string xmp_buf = img->xmp->xmpBytes();
      if (!xmp_buf.empty()) { write_xmp_box(&jp2_ultimate_tgt, xmp_buf.c_str()); }
    }

    jpx_out.write_headers();
    jp2_output_box *out_box = jpx_stream.open_stream();

    codestream.access_siz()->finalize_all();


    //
    // here we calculate the JPEG2000/kakadu parameters for num_layer...
    // see kdu_compress, derived fromn code there
    //
    /*
    int num_layers = 0;
    std::vector<kdu_long> layer_sizes;
    kdu_long *layer_sizes_ptr = nullptr;
    if (rates.size() > 0) {
        if ((rates.size() == num_clayers) || ((rates.size() <= 2) && ((num_clayers >= 2)))) {
            kdu_long total_pels = get_bpp_dims(codestream);
            for (const auto &rate: rates) {
                if (rate == -1.0) {
                    layer_sizes.push_back(KDU_LONG_MAX);
                } else {
                    layer_sizes.push_back((kdu_long) std::floor(rate * 0.125 * total_pels));
                }
            }
            std::sort(layer_sizes.begin(), layer_sizes.end());
            if (layer_sizes.back() == KDU_LONG_MAX) layer_sizes.back() = 0;
            layer_sizes_ptr = layer_sizes.data();
            num_layers = layer_sizes.size();
        }
    } else if (num_clayers > 0) {
        for (int i = 0; i < num_clayers; i++) {
            layer_sizes.push_back(0);
        }
        layer_sizes_ptr = layer_sizes.data();
        num_layers = layer_sizes.size();
    } else {
        layer_sizes_ptr = nullptr;
        num_layers = 0;
    }
    */
    // Now compress the image in one hit, using `kdu_stripe_compressor'
    kdu_stripe_compressor compressor;
    compressor.mem_configure(&membroker);
    compressor.start(codestream,
      0,// num_layers,       // num_layer_specs
      nullptr,// layer_sizes_ptr, // layer_sizes
      nullptr,// layer_slopes
      0,// min_slope_threshold
      true,// no_prediction
      is_reversible,// force_precise [YES, if reversible=yes]
      true,// record_layer_info_in_comment
      0.0,// size_tolerance
      img->nc,// num_components
      false,// want_fastest [NO]
      env_ref);

    // int *stripe_heights = new int[img->nc];
    int stripe_heights[5];
    int *precisions;
    bool *is_signed;
    if (img->bps == 16) {
      kdu_int16 *buf = (kdu_int16 *)img->pixels;
      precisions = new int[img->nc];
      is_signed = new bool[img->nc];
      for (size_t i = 0; i < img->nc; i++) {
        precisions[i] = img->bps;
        is_signed[i] = false;
      }
      for (size_t i = 0; i < img->nc; i++) { stripe_heights[i] = img->ny; }
      compressor.push_stripe(buf, stripe_heights, nullptr, nullptr, nullptr, precisions, is_signed);
    } else if (img->bps == 8) {
      if (th == 0) th = img->ny;
      size_t stripe_start = 0;
      do {
        kdu_byte *buf = (kdu_byte *)img->pixels + stripe_start * img->nc * img->nx;
        for (size_t i = 0; i < img->nc; i++) { stripe_heights[i] = th; }
        compressor.push_stripe(buf, stripe_heights);
        stripe_start += th;
      } while ((img->ny - stripe_start) >= th);
      if ((img->ny - stripe_start) > 0) {
        kdu_byte *buf = (kdu_byte *)img->pixels + stripe_start * img->nc * img->nx;
        for (size_t i = 0; i < img->nc; i++) { stripe_heights[i] = img->ny - stripe_start; }
        compressor.push_stripe(buf, stripe_heights);
        stripe_start += img->ny - stripe_start;
      }
    } else {
      throw SipiImageError("Unsupported number of bits/sample!");
    }
    compressor.finish(0, NULL, NULL, env_ref);
    // Finally, cleanup
    codestream.destroy();// All done: simple as that.
    output->close();// Not really necessary here.
    jpx_out.close();
    if (jp2_ultimate_tgt.exists()) { jp2_ultimate_tgt.close(); }
    // delete[] stripe_heights;
    if (img->bps == 16) {
      delete[] precisions;
      delete[] is_signed;
    }
    if (http != nullptr) { delete http; }
  } catch (kdu_exception e) {
    throw SipiImageError("Problem writing a JPEG2000 image!");
  }
}
}// namespace Sipi
