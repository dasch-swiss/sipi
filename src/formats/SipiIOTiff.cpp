/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <cassert>
#include <cstdarg>
#include <cstdlib>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include <cerrno>

#include "tif_dir.h"// libtiff internals; for _TIFFFieldArray

#include "shttps/Connection.h"

#include "Logger.h"
#include "SipiError.hpp"
#include "SipiImage.hpp"
#include "SipiImageError.hpp"
#include "formats/SipiIOTiff.h"


#include "shttps/Global.h"

#define TIFF_GET_FIELD(file, tag, var, default)                      \
  {                                                                  \
    if (0 == TIFFGetField((file), (tag), (var))) *(var) = (default); \
  }

extern "C" {

typedef struct _memtiff
{
  unsigned char *data;
  tsize_t size;
  tsize_t incsiz;
  tsize_t flen;
  toff_t fptr;
} MEMTIFF;

static MEMTIFF *memTiffOpen(tsize_t incsiz = 10240, tsize_t initsiz = 10240)
{
  MEMTIFF *memtif;
  if ((memtif = (MEMTIFF *)malloc(sizeof(MEMTIFF))) == nullptr) { throw Sipi::SipiImageError("malloc failed", errno); }

  memtif->incsiz = incsiz;

  if (initsiz == 0) initsiz = incsiz;

  if ((memtif->data = (unsigned char *)malloc(initsiz * sizeof(unsigned char))) == nullptr) {
    free(memtif);
    throw Sipi::SipiImageError("malloc failed", errno);
  }

  memtif->size = initsiz;
  memtif->flen = 0;
  memtif->fptr = 0;
  return memtif;
}
/*===========================================================================*/

static tsize_t memTiffReadProc(thandle_t handle, tdata_t buf, tsize_t size)
{
  auto *memtif = (MEMTIFF *)handle;

  tsize_t n;

  if (((tsize_t)memtif->fptr + size) <= memtif->flen) {
    n = size;
  } else {
    n = memtif->flen - static_cast<tsize_t>(memtif->fptr);
  }

  memcpy(buf, memtif->data + memtif->fptr, n);
  memtif->fptr += n;

  return n;
}
/*===========================================================================*/

static tsize_t memTiffWriteProc(thandle_t handle, tdata_t buf, tsize_t size)
{
  auto *memtif = (MEMTIFF *)handle;

  if (((tsize_t)memtif->fptr + size) > memtif->size) {
    if ((memtif->data = (unsigned char *)realloc(memtif->data, memtif->fptr + memtif->incsiz + size)) == nullptr) {
      throw Sipi::SipiImageError("realloc failed", errno);
    }

    memtif->size = memtif->fptr + memtif->incsiz + size;
  }

  memcpy(memtif->data + memtif->fptr, buf, size);
  memtif->fptr += size;

  if (memtif->fptr > memtif->flen) memtif->flen = memtif->fptr;

  return size;
}
/*===========================================================================*/

static toff_t memTiffSeekProc(thandle_t handle, toff_t off, int whence)
{
  auto *memtif = (MEMTIFF *)handle;

  switch (whence) {
  case SEEK_SET: {
    if ((tsize_t)off > memtif->size) {
      if ((memtif->data = (unsigned char *)realloc(memtif->data, memtif->size + memtif->incsiz + off)) == nullptr) {
        throw Sipi::SipiImageError("realloc failed", errno);
      }

      memtif->size = memtif->size + memtif->incsiz + off;
    }

    memtif->fptr = off;
    break;
  }
  case SEEK_CUR: {
    if ((tsize_t)(memtif->fptr + off) > memtif->size) {
      if ((memtif->data = (unsigned char *)realloc(memtif->data, memtif->fptr + memtif->incsiz + off)) == nullptr) {
        throw Sipi::SipiImageError("realloc failed", errno);
      }

      memtif->size = memtif->fptr + memtif->incsiz + off;
    }

    memtif->fptr += off;
    break;
  }
  case SEEK_END: {
    if ((tsize_t)(memtif->size + off) > memtif->size) {
      if ((memtif->data = (unsigned char *)realloc(memtif->data, memtif->size + memtif->incsiz + off)) == nullptr) {
        throw Sipi::SipiImageError("realloc failed", errno);
      }

      memtif->size = memtif->size + memtif->incsiz + off;
    }

    memtif->fptr = memtif->size + off;
    break;
  }
  default: {
  }
  }

  if (memtif->fptr > memtif->flen) memtif->flen = memtif->fptr;
  return memtif->fptr;
}
/*===========================================================================*/

static int memTiffCloseProc(thandle_t handle)
{
  auto *memtif = (MEMTIFF *)handle;
  memtif->fptr = 0;
  return 0;
}
/*===========================================================================*/


static toff_t memTiffSizeProc(thandle_t handle)
{
  auto *memtif = (MEMTIFF *)handle;
  return memtif->flen;
}
/*===========================================================================*/


static int memTiffMapProc(thandle_t handle, tdata_t *base, toff_t *psize)
{
  auto *memtif = (MEMTIFF *)handle;
  *base = memtif->data;
  *psize = memtif->flen;
  return (1);
}
/*===========================================================================*/

static void memTiffUnmapProc(thandle_t handle, tdata_t base, toff_t size) {}
/*===========================================================================*/

static void memTiffFree(MEMTIFF *memtif)
{
  if (memtif->data != nullptr) free(memtif->data);
  memtif->data = nullptr;
  if (memtif != nullptr) free(memtif);
  memtif = nullptr;
}
/*===========================================================================*/
}


//
// the 2 typedefs below are used to extract the EXIF-tags from a TIFF file. This is done
// using the normal libtiff functions...
//
typedef enum {
  EXIF_DT_UINT8 = 1,
  EXIF_DT_STRING = 2,
  EXIF_DT_UINT16 = 3,
  EXIF_DT_UINT32 = 4,
  EXIF_DT_RATIONAL = 5,
  EXIF_DT_2ST = 7,

  EXIF_DT_RATIONAL_PTR = 101,
  EXIF_DT_UINT8_PTR = 102,
  EXIF_DT_UINT16_PTR = 103,
  EXIF_DT_UINT32_PTR = 104,
  EXIF_DT_PTR = 105,
  EXIF_DT_UNDEFINED = 999

} ExifDataType_type;

typedef struct _exif_tag
{
  uint16_t tag_id;
  ExifDataType_type datatype;
  int len;
  union {
    float f_val;
    uint8_t c_val;
    uint16_t s_val;
    uint32_t i_val;
    char *str_val;
    float *f_ptr;
    uint8_t *c_ptr;
    uint16_t *s_ptr;
    uint32_t *i_ptr;
    void *ptr;
    unsigned char _4cc[4];
    unsigned short _2st[2];
  };
} ExifTag_type;

static ExifTag_type exiftag_list[] = {
  { EXIFTAG_EXPOSURETIME, EXIF_DT_RATIONAL, 0L, { 0L } },
  { EXIFTAG_FNUMBER, EXIF_DT_RATIONAL, 0L, { 0L } },
  { EXIFTAG_EXPOSUREPROGRAM, EXIF_DT_UINT16, 0L, { 0L } },
  { EXIFTAG_SPECTRALSENSITIVITY, EXIF_DT_STRING, 0L, { 0L } },
  { EXIFTAG_ISOSPEEDRATINGS, EXIF_DT_UINT16_PTR, 0L, { 0L } },
  { EXIFTAG_OECF, EXIF_DT_PTR, 0L, { 0L } },
  { EXIFTAG_EXIFVERSION, EXIF_DT_UNDEFINED, 0L, { 0L } },
  { EXIFTAG_DATETIMEORIGINAL, EXIF_DT_STRING, 0L, { 0L } },
  { EXIFTAG_DATETIMEDIGITIZED, EXIF_DT_STRING, 0L, { 0L } },
  { EXIFTAG_COMPONENTSCONFIGURATION, EXIF_DT_UNDEFINED, 0L, { 1L } },// !!!! would be 4cc
  { EXIFTAG_COMPRESSEDBITSPERPIXEL, EXIF_DT_RATIONAL, 0L, { 0L } },
  { EXIFTAG_SHUTTERSPEEDVALUE, EXIF_DT_RATIONAL, 0L, { 0L } },
  { EXIFTAG_APERTUREVALUE, EXIF_DT_RATIONAL, 0L, { 0l } },
  { EXIFTAG_BRIGHTNESSVALUE, EXIF_DT_RATIONAL, 0L, { 0l } },
  { EXIFTAG_EXPOSUREBIASVALUE, EXIF_DT_RATIONAL, 0L, { 0l } },
  { EXIFTAG_MAXAPERTUREVALUE, EXIF_DT_RATIONAL, 0L, { 0l } },
  { EXIFTAG_SUBJECTDISTANCE, EXIF_DT_RATIONAL, 0L, { 0l } },
  { EXIFTAG_METERINGMODE, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_LIGHTSOURCE, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_FLASH, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_FOCALLENGTH, EXIF_DT_RATIONAL, 0L, { 0l } },
  { EXIFTAG_SUBJECTAREA,
    EXIF_DT_UINT16_PTR,
    0L,
    { 0L } },//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! ARRAY OF SHORTS
  { EXIFTAG_MAKERNOTE, EXIF_DT_UNDEFINED, 0L, { 0L } },
  { EXIFTAG_USERCOMMENT, EXIF_DT_PTR, 0L, { 0L } },
  { EXIFTAG_SUBSECTIME, EXIF_DT_STRING, 0L, { 0L } },
  { EXIFTAG_SUBSECTIMEORIGINAL, EXIF_DT_STRING, 0L, { 0L } },
  { EXIFTAG_SUBSECTIMEDIGITIZED, EXIF_DT_STRING, 0L, { 0L } },
  { EXIFTAG_FLASHPIXVERSION, EXIF_DT_UNDEFINED, 0L, { 01L } },// 2 SHORTS
  { EXIFTAG_COLORSPACE, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_PIXELXDIMENSION, EXIF_DT_UINT32, 0L, { 0l } },// CAN ALSO BE UINT16 !!!!!!!!!!!!!!
  { EXIFTAG_PIXELYDIMENSION, EXIF_DT_UINT32, 0L, { 0l } },// CAN ALSO BE UINT16 !!!!!!!!!!!!!!
  { EXIFTAG_RELATEDSOUNDFILE, EXIF_DT_STRING, 0L, { 0L } },
  { EXIFTAG_FLASHENERGY, EXIF_DT_RATIONAL, 0L, { 0l } },
  { EXIFTAG_SPATIALFREQUENCYRESPONSE, EXIF_DT_PTR, 0L, { 0L } },
  { EXIFTAG_FOCALPLANEXRESOLUTION, EXIF_DT_RATIONAL, 0L, { 0l } },
  { EXIFTAG_FOCALPLANEYRESOLUTION, EXIF_DT_RATIONAL, 0L, { 0l } },
  { EXIFTAG_FOCALPLANERESOLUTIONUNIT, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_SUBJECTLOCATION, EXIF_DT_UINT32, 0L, { 0l } },// 2 SHORTS !!!!!!!!!!!!!!!!!!!!!!!!!!!
  { EXIFTAG_EXPOSUREINDEX, EXIF_DT_RATIONAL, 0L, { 0l } },
  { EXIFTAG_SENSINGMETHOD, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_FILESOURCE, EXIF_DT_UINT8, 0L, { 0L } },
  { EXIFTAG_SCENETYPE, EXIF_DT_UINT8, 0L, { 0L } },
  { EXIFTAG_CFAPATTERN, EXIF_DT_PTR, 0L, { 0L } },
  { EXIFTAG_CUSTOMRENDERED, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_EXPOSUREMODE, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_WHITEBALANCE, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_DIGITALZOOMRATIO, EXIF_DT_RATIONAL, 0L, { 0l } },
  { EXIFTAG_FOCALLENGTHIN35MMFILM, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_SCENECAPTURETYPE, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_GAINCONTROL, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_CONTRAST, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_SATURATION, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_SHARPNESS, EXIF_DT_UINT16, 0L, { 0l } },
  { EXIFTAG_DEVICESETTINGDESCRIPTION, EXIF_DT_PTR, 0L, { 0L } },
  { EXIFTAG_SUBJECTDISTANCERANGE, EXIF_DT_UINT16, 0L, { 0L } },
  { EXIFTAG_IMAGEUNIQUEID, EXIF_DT_STRING, 0L, { 0L } },
};

static int exiftag_list_len = sizeof(exiftag_list) / sizeof(ExifTag_type);


namespace Sipi {

unsigned char *read_watermark(const std::string &wmfile, int &nx, int &ny, int &nc)
{
  TIFF *tif;
  int sll;
  unsigned short spp, bps, pmi, pc;
  byte *wmbuf;
  nx = 0;
  ny = 0;

  if (nullptr == (tif = TIFFOpen(wmfile.c_str(), "r"))) { return nullptr; }

  // add EXIF tags to the set of tags that libtiff knows about
  // necessary if we want to set EXIFTAG_DATETIMEORIGINAL, for example
  // const TIFFFieldArray *exif_fields = _TIFFGetExifFields();
  //_TIFFMergeFields(tif, exif_fields->fields, exif_fields->count);


  if (TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &nx) == 0) {
    TIFFClose(tif);
    throw Sipi::SipiImageError("ERROR in read_watermark: TIFFGetField of TIFFTAG_IMAGEWIDTH failed: " + wmfile);
  }

  if (TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &ny) == 0) {
    TIFFClose(tif);
    throw Sipi::SipiImageError("ERROR in read_watermark: TIFFGetField of TIFFTAG_IMAGELENGTH failed: " + wmfile);
  }

  TIFF_GET_FIELD(tif, TIFFTAG_SAMPLESPERPIXEL, &spp, 1);

  TIFF_GET_FIELD(tif, TIFFTAG_BITSPERSAMPLE, &bps, 1);

  if (bps != 8) {
    TIFFClose(tif);
    throw Sipi::SipiImageError("ERROR in read_watermark: bps ≠ 8: " + wmfile);
  }

  TIFF_GET_FIELD(tif, TIFFTAG_PHOTOMETRIC, &pmi, PHOTOMETRIC_MINISBLACK);
  TIFF_GET_FIELD(tif, TIFFTAG_PLANARCONFIG, &pc, PLANARCONFIG_CONTIG);

  if (pc != PLANARCONFIG_CONTIG) {
    TIFFClose(tif);
    throw Sipi::SipiImageError(
      "ERROR in read_watermark: Tag TIFFTAG_PLANARCONFIG is not PLANARCONFIG_CONTIG: " + wmfile);
  }

  sll = nx * spp * bps / 8;

  try {
    wmbuf = new byte[ny * sll];
  } catch (std::bad_alloc &ba) {
    throw Sipi::SipiImageError("ERROR in read_watermark: Could not allocate memory: ");// + ba.what());
  }

  int cnt = 0;

  for (int i = 0; i < ny; i++) {
    if (TIFFReadScanline(tif, wmbuf + i * sll, i) == -1) {
      delete[] wmbuf;
      throw Sipi::SipiImageError(
        "ERROR in read_watermark: TIFFReadScanline failed on scanline " + std::to_string(i) + " in file " + wmfile);
    }

    for (int ii = 0; ii < sll; ii++) {
      if (wmbuf[i * sll + ii] > 0) { cnt++; }
    }
  }

  TIFFClose(tif);
  nc = spp;

  return wmbuf;
}
//============================================================================


static void tiffError(const char *module, const char *fmt, va_list args)
{
  // silenced due to erroneous UTF8 sequences in fmt values, leading to segfaults, perhaps fixable.

  /* log_err("ERROR IN TIFF! Module: %s", module); */
  /* log_err(fmt, argptr); */
}
//============================================================================


static void tiffWarning(const char *module, const char *fmt, va_list args)
{
  // silenced due to erroneous UTF8 sequences in fmt values, leading to segfaults, perhaps fixable.

  /* log_err("ERROR IN TIFF! Module: %s", module); */
  /* log_err(fmt, argptr); */
}
//============================================================================

#define N(a) (sizeof(a) / sizeof(a[0]))
#define TIFFTAG_SIPIMETA 65111

static const TIFFFieldInfo xtiffFieldInfo[] = {
  { TIFFTAG_SIPIMETA, 1, 1, TIFF_ASCII, FIELD_CUSTOM, 1, 0, const_cast<char *>("SipiEssentialMetadata") },
};
//============================================================================

static TIFFExtendProc parent_extender = nullptr;

static void registerCustomTIFFTags(TIFF *tif)
{
  /* Install the extended Tag field info */
  TIFFMergeFieldInfo(tif, xtiffFieldInfo, N(xtiffFieldInfo));
  if (parent_extender != nullptr) (*parent_extender)(tif);
}
//============================================================================

void SipiIOTiff::initLibrary()
{
  static bool done = false;
  if (!done) {
    TIFFSetErrorHandler(tiffError);
    TIFFSetWarningHandler(tiffWarning);

    parent_extender = TIFFSetTagExtender(registerCustomTIFFTags);
    done = true;
  }
}
//============================================================================

template<typename T> void one2eight(const uint8_t *in, T *out, uint32_t len, uint8_t black, uint8_t white)
{
  static uint8_t mask[8] = {
    0b10000000, 0b01000000, 0b00100000, 0b00010000, 0b00001000, 0b00000100, 0b00000010, 0b00000001
  };
  uint32_t ii = 0;
  for (uint32_t i = 0; i < len; i += 8) {
    for (uint32_t k = 0; (k < 8) && ((k + i) < len); ++k) { out[i + k] = mask[k] & in[ii] ? white : black; }
    ++ii;
  }
}

template<typename T> void four2eight(const uint8_t *in, T *out, uint32_t len, bool is_palette = false)
{
  static uint8_t mask[2] = { 0b11110000, 0b00001111 };

  if (is_palette) {
    uint32_t ii = 0;
    for (uint32_t i = 0; i < len; i += 2, ++ii) {
      out[i] = (mask[0] & in[ii]) >> 4;
      if ((i + 1) < len) { out[i + 1] = mask[1] & in[ii]; }
    }
  } else {
    uint32_t ii = 0;
    for (uint32_t i = 0; i < len; i += 2, ++ii) {
      out[i] = mask[0] & in[ii];
      if ((i + 1) < len) { out[i + 1] = (mask[1] & in[ii]) << 4; }
    }
  }
}

template<typename T> void twelve2sixteen(const uint8_t *in, T *out, uint32_t len, bool is_palette = false)
{
  static uint8_t mask[2] = { 0b11110000, 0b00001111 };
  if (is_palette) {
    uint32_t ii = 0;
    for (uint32_t i = 0; i < len; i += 2, ii += 3) {
      out[i] = (in[ii] << 4) | ((in[ii + 1] & mask[0]) >> 4);
      if ((i + 1) < len) { out[i + 1] = ((in[ii + 1] & mask[1]) << 8) | in[ii + 2]; }
    }
  } else {
    uint32_t ii = 0;
    for (uint32_t i = 0; i < len; i += 2, ii += 3) {
      out[i] = (in[ii] << 8) | (in[ii + 1] & mask[0]);
      if ((i + 1) < len) { out[i + 1] = ((in[ii + 1] & mask[1]) << 12) | (in[ii + 2] << 4); }
    }
  }
}

template<typename T>
std::unique_ptr<T> separateToContig(std::unique_ptr<T> &&inbuf, uint32_t nx, uint32_t ny, uint32_t nc, uint32_t sll)
{
  auto tmpptr = std::make_unique<T>(nc * ny * nx);
  for (uint32_t c = 0; c < nc; ++c) {
    for (uint32_t y = 0; y < ny; ++y) {
      for (uint32_t x = 0; x < nx; ++x) { tmpptr[nc * (y * nx + x) + c] = inbuf.get()[c * ny * sll + y * nx + x]; }
    }
  }
  return tmpptr;
}

template<typename T>
std::vector<T> separateToContig(std::vector<T> &&inbuf, uint32_t nx, uint32_t ny, uint32_t nc, uint32_t sll)
{
  auto tmpptr = std::vector<T>(nc * ny * nx);
  for (uint32_t c = 0; c < nc; ++c) {
    for (uint32_t y = 0; y < ny; ++y) {
      for (uint32_t x = 0; x < nx; ++x) { tmpptr[nc * (y * nx + x) + c] = inbuf[c * ny * sll + y * nx + x]; }
    }
  }
  return tmpptr;
}

template<typename T>
static std::vector<T> read_standard_data(TIFF *tif, int32_t roi_x, int32_t roi_y, uint32_t roi_w, uint32_t roi_h)
{
  uint16_t planar;
  TIFF_GET_FIELD(tif, TIFFTAG_PLANARCONFIG, &planar, PLANARCONFIG_CONTIG)
  uint16_t compression;
  TIFF_GET_FIELD(tif, TIFFTAG_COMPRESSION, &compression, COMPRESSION_NONE)
  auto sll = static_cast<uint32_t>(TIFFScanlineSize(tif));

  uint32_t nx, ny, nc, bps;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &nx);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &ny);
  uint16_t stmp;
  TIFF_GET_FIELD(tif, TIFFTAG_SAMPLESPERPIXEL, &stmp, 1)
  nc = static_cast<uint32_t>(stmp);

  TIFF_GET_FIELD(tif, TIFFTAG_BITSPERSAMPLE, &stmp, 8)
  bps = static_cast<uint32_t>(stmp);

  PhotometricInterpretation photo;
  if (1 != TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &stmp)) {
    photo = PhotometricInterpretation::MINISBLACK;
  } else {
    photo = static_cast<PhotometricInterpretation>(stmp);
  }

  uint8_t black, white;
  if (photo == PhotometricInterpretation::MINISBLACK) {
    black = 0x00;// 0b0 -> 0x00
    white = 0xff;// 0b1 -> 0xff
  } else if (photo == PhotometricInterpretation::MINISWHITE) {
    black = 0xff;// 0b0 -> 0xff
    white = 0x00;// 0b1 -> 0x00
  }

  uint32_t psiz;
  if (bps <= 8) {// 1, 4, 8 bit -> 8 bit
    psiz = sizeof(uint8_t);
  } else {// 12, 16 bit -> 16 bit
    psiz = sizeof(uint16_t);
  }

  std::vector<T> inbuf(roi_h * roi_w * nc);
  auto scanline = std::make_unique<uint8_t[]>(sll);
  std::unique_ptr<T[]> line;
  if (compression == COMPRESSION_NONE) {
    if (planar == PLANARCONFIG_CONTIG) {// RGBRGBRGBRGB...
      line = std::make_unique<T[]>(nx * nc);
      for (uint32_t i = roi_y; i < roi_h; ++i) {
        if (TIFFReadScanline(tif, scanline.get(), i, 0) != 1) {
          TIFFClose(tif);
          throw Sipi::SipiImageError("TIFFReadScanline failed on scanline {}" + std::to_string(i));
        }
        switch (bps) {
        case 1:
          one2eight<T>(scanline.get(), line.get(), nc * nx, black, white);
          std::memcpy(inbuf.data() + nc * i * roi_w, line.get() + nc * roi_x, nc * roi_w);
          break;
        case 4:
          four2eight<T>(scanline.get(), line.get(), nc * nx, photo == PhotometricInterpretation::PALETTE);
          std::memcpy(inbuf.data() + nc * i * roi_w, line.get() + nc * roi_x, nc * roi_w);
          break;
        case 8:
          std::memcpy(inbuf.data() + nc * i * roi_w, scanline.get() + nc * roi_x, nc * roi_w);
          break;
        case 12:
          twelve2sixteen<T>(scanline.get(), line.get(), nc * nx, photo == PhotometricInterpretation::PALETTE);
          std::memcpy(inbuf.data() + nc * i * roi_w, line.get() + nc * roi_x, nc * roi_w * psiz);
          break;
        case 16:
          std::memcpy(inbuf.data() + nc * i * roi_w, scanline.get() + nc * roi_x * psiz, nc * roi_w * psiz);
          break;
        default:;
        }
      }
    } else if (planar == PLANARCONFIG_SEPARATE) {// RRRRR…RRR GGGGG…GGGG BBBBB…BBB
      line = std::make_unique<T[]>(nx);
      for (uint32_t c = 0; c < nc; ++c) {
        for (uint32_t i = roi_y; i < roi_h; ++i) {
          if (TIFFReadScanline(tif, scanline.get(), i, c) == -1) {
            TIFFClose(tif);
            throw Sipi::SipiImageError("TIFFReadScanline failed on scanline {}" + std::to_string(i));
          }
          switch (bps) {
          case 1:
            one2eight<T>(scanline.get(), line.get(), nx, black, white);
            std::memcpy(inbuf.data() + nc * roi_w + roi_h + i * roi_w, line.get() + roi_x, roi_w);
            break;
          case 4:
            four2eight<T>(scanline.get(), line.get(), nx, photo == PhotometricInterpretation::PALETTE);
            std::memcpy(inbuf.data() + nc * roi_w + roi_h + i * roi_w, line.get() + roi_x, roi_w);
            break;
          case 8:
            std::memcpy(inbuf.data() + nc * roi_w + roi_h + i * roi_w, scanline.get() + roi_x, roi_w);
            break;
          case 12:
            twelve2sixteen<T>(scanline.get(), line.get(), nx, photo == PhotometricInterpretation::PALETTE);
            std::memcpy(inbuf.data() + nc * roi_w + roi_h + i * roi_w, line.get() + roi_x, roi_w * psiz);
            break;
          case 16:
            std::memcpy(inbuf.data() + nc * roi_w + roi_h + i * roi_w, scanline.get() + roi_x * psiz, roi_w * psiz);
            break;
          default:;
          }
        }
      }
      inbuf = separateToContig<T>(std::move(inbuf), roi_w, roi_h, nc, roi_w);
    }
  } else {// we do have compression....
    if (planar == PLANARCONFIG_CONTIG) {// RGBRGBRGBRGB...
      line = std::make_unique<T[]>(nx * nc);
      for (uint32_t i = 0; i < ny; ++i) {
        if (TIFFReadScanline(tif, scanline.get(), i, 0) != 1) {
          TIFFClose(tif);
          throw Sipi::SipiImageError("TIFFReadScanline failed on scanline {}" + std::to_string(i));
        }
        if ((i >= roi_y) && (i < (roi_y + roi_h))) {
          switch (bps) {
          case 1:
            one2eight<T>(scanline.get(), line.get(), nc * nx, black, white);
            std::memcpy(inbuf.data() + nc * i * roi_w, line.get() + nc * roi_x, nc * roi_w);
            break;
          case 4:
            four2eight<T>(scanline.get(), line.get(), nc * nx, photo == PhotometricInterpretation::PALETTE);
            std::memcpy(inbuf.data() + nc * i * roi_w, line.get() + nc * roi_x, nc * roi_w);
            break;
          case 8:
            std::memcpy(inbuf.data() + nc * (i - roi_y) * roi_w, scanline.get() + nc * roi_x, nc * roi_w);
            break;
          case 12:
            twelve2sixteen<T>(scanline.get(), line.get(), nc * nx, photo == PhotometricInterpretation::PALETTE);
            std::memcpy(inbuf.data() + nc * i * roi_w, line.get() + nc * roi_x * psiz, nc * roi_w * psiz);
            break;
          case 16:
            std::memcpy(inbuf.data() + nc * i * roi_w, scanline.get() + nc * roi_x * psiz, nc * roi_w * psiz);
            break;
          default:;
          }
        }
      }
    } else if (planar == PLANARCONFIG_SEPARATE) {// RRRRR…RRR GGGGG…GGGG BBBBB…BBB
      line = std::make_unique<T[]>(nx);
      for (uint32_t c = 0; c < nc; ++c) {
        for (uint32_t i = 0; i < ny; ++i) {
          if (TIFFReadScanline(tif, scanline.get(), i, c) == -1) {
            TIFFClose(tif);
            throw Sipi::SipiImageError("TIFFReadScanline failed on scanline {}" + std::to_string(i));
          }
          if ((i >= roi_y) && (i < (roi_y + roi_h))) {
            switch (bps) {
            case 1:
              one2eight<T>(scanline.get(), line.get(), nx, black, white);
              std::memcpy(inbuf.data() + nc * roi_w + roi_h + i * roi_w, line.get() + roi_x, roi_w);
              break;
            case 4:
              four2eight<T>(scanline.get(), line.get(), nx, photo == PhotometricInterpretation::PALETTE);
              std::memcpy(inbuf.data() + nc * roi_w + roi_h + i * roi_w, line.get() + roi_x, roi_w);
              break;
            case 8:
              std::memcpy(inbuf.data() + nc * roi_w + roi_h + i * roi_w, scanline.get() + roi_x, roi_w);
              break;
            case 12:
              twelve2sixteen<T>(scanline.get(), line.get(), nx, photo == PhotometricInterpretation::PALETTE);
              std::memcpy(inbuf.data() + nc * roi_w + roi_h + i * roi_w, line.get() + roi_x * psiz, roi_w * psiz);
              break;
            case 16:
              std::memcpy(inbuf.data() + nc * roi_w + roi_h + i * roi_w, scanline.get() + roi_x * psiz, roi_w * psiz);
              break;
            default:;
            }
          }
        }
      }
      inbuf = separateToContig<T>(std::move(inbuf), roi_w, roi_h, nc, roi_w);
    }
  }
  return inbuf;
}

static const float epsilon = 1.0e-4;

size_t epsilon_ceil(float a)
{
  if (fabs(floor(a) - a) < epsilon) { return static_cast<size_t>(floorf(a)); }
  return static_cast<size_t>(ceilf(a));
}

size_t epsilon_ceil_division(float a, float b)
{
  //
  // epsilontic:
  // if a/b is x.00002, the result will be floorf(x), otherwise ceilf(x)
  //
  if (fabs(floorf(a / b) - (a / b)) < epsilon) { return static_cast<size_t>(floorf(a / b)); }
  return static_cast<size_t>(ceilf(a / b));
}

size_t epsilon_floor(float a)
{
  if (fabs(ceilf(a) - a) < epsilon) { return static_cast<size_t>(ceilf(a)); }
  return static_cast<size_t>(floorf(a));
}

size_t epsilon_floor_division(float a, float b)
{
  //
  // epsilontic:
  // if a/b is x.9998, the result will be ceilf(x), otherwise floor(x)
  //
  if (fabs(ceilf(a / b) - (a / b)) < epsilon) { return static_cast<size_t>(ceilf(a / b)); }
  return static_cast<size_t>(floorf(a / b));
}

template<typename T>
static std::vector<T> read_tiled_data(TIFF *tif, int32_t roi_x, int32_t roi_y, uint32_t roi_w, uint32_t roi_h)
{
  uint16_t planar;
  TIFF_GET_FIELD(tif, TIFFTAG_PLANARCONFIG, &planar, PLANARCONFIG_CONTIG)
  uint32_t tile_width;
  uint32_t tile_length;
  TIFF_GET_FIELD(tif, TIFFTAG_TILEWIDTH, &tile_width, 0)
  TIFF_GET_FIELD(tif, TIFFTAG_TILELENGTH, &tile_length, 0)
  if ((tile_width == 0) || (tile_length == 0)) {
    throw Sipi::SipiImageError("Expected tiled image, but no tile dimension given!");
  }

  uint32_t nx, ny, nc, bps;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &nx);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &ny);
  uint32_t ntiles_x = epsilon_ceil_division(static_cast<float>(nx), static_cast<float>(tile_width));
  uint32_t ntiles_y = epsilon_ceil_division(static_cast<float>(ny), static_cast<float>(tile_length));
  uint32_t ntiles = TIFFNumberOfTiles(tif);
  if (ntiles != (ntiles_x * ntiles_y)) { throw Sipi::SipiImageError("Number of tiles no consistent!"); }
  uint32_t starttile_x = epsilon_floor_division(static_cast<float>(roi_x), static_cast<float>(tile_width));
  uint32_t starttile_y = epsilon_floor_division(static_cast<float>(roi_y), static_cast<float>(tile_length));
  uint32_t endtile_x = epsilon_ceil_division(static_cast<float>(roi_x + roi_w), static_cast<float>(tile_width));
  uint32_t endtile_y = epsilon_ceil_division(static_cast<float>(roi_y + roi_h), static_cast<float>(tile_length));

  uint16_t stmp;
  TIFF_GET_FIELD(tif, TIFFTAG_SAMPLESPERPIXEL, &stmp, 1)
  nc = static_cast<uint32_t>(stmp);

  TIFF_GET_FIELD(tif, TIFFTAG_BITSPERSAMPLE, &stmp, 8)
  bps = static_cast<uint32_t>(stmp);

  if ((bps != 8) && (bps != 16)) {
    throw Sipi::SipiImageError("{} bits per samples not supported for tiled tiffs!" + std::to_string(bps));
  }

  uint32_t tile_size = TIFFTileSize(tif);
  auto tilebuf = std::make_unique<T[]>(bps == 8 ? tile_size : (tile_size >> 1));
  auto inbuf = std::vector<T>(roi_w * roi_h * nc);
  for (uint32_t ty = starttile_y; ty < endtile_y; ++ty) {
    for (uint32_t tx = starttile_x; tx < endtile_x; ++tx) {
      if (TIFFReadTile(tif, tilebuf.get(), tx * tile_width, ty * tile_length, 0, 0) < 0) {
        TIFFClose(tif);
        throw Sipi::SipiImageError("TIFFReadTile failed on tile ({}, {})" + std::to_string(tx) + std::to_string(ty));
      }

      if (planar == PLANARCONFIG_SEPARATE) {
        tilebuf = separateToContig(std::move(tilebuf), tile_width, tile_length, nc, tile_width);
      }

      for (uint32_t tile_x = 0; tile_x < tile_width; tile_x++) {
        for (uint32_t tile_y = 0; tile_y < tile_length; tile_y++) {
          uint32_t final_x = (tx - starttile_x) * tile_width + tile_x;
          uint32_t final_y = (ty - starttile_y) * tile_length + tile_y;

          if (final_x < roi_w && final_y < roi_h) {
            uint32_t pixel_offset = nc * (final_y * roi_w + final_x);

            for (uint32_t c = 0; c < nc; ++c) {
              inbuf[pixel_offset + c] = tilebuf[(tile_x + tile_y * tile_width) * nc + c];
            }
          }
        }
      }
    }
  }
  return inbuf;
}
// get the resolutions of pyramid if available
std::vector<SubImageInfo> read_resolutions(uint64_t image_width, TIFF *tif)
{
  std::vector<SubImageInfo> resolutions;
  do {
    uint32_t tmp_width;
    uint32_t tmp_height;
    uint32_t tile_width;
    uint32_t tile_length;
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &tmp_width);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &tmp_height);
    if (TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width) != 1) { tile_width = 0; }
    if (TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_length) != 1) { tile_length = 0; }
    uint32_t reduce_w = std::lroundf(static_cast<float>(image_width) / static_cast<float>(tmp_width));
    uint32_t reduce = reduce_w;
    resolutions.push_back({ reduce, tmp_width, tmp_height, tile_width, tile_length });
  } while (TIFFReadDirectory(tif));

  TIFFSetDirectory(tif, 0);

  return resolutions;
}

#include <iostream>
std::ostream &operator<<(std::ostream &os, const SubImageInfo &s)
{
  return os << "Reduce: " << s.reduce << ", "
            << "Width: " << s.width << ", "
            << "Height: " << s.height << ", "
            << "TW: " << s.tile_width << ", "
            << "TH:" << s.tile_height;
}

/**
 * TODO: SipiImage always assumes the image data to be in big endian format.
 * TIFF files can be in little endian format. Every TIFF file begins with a two-byte indicator of byte order:
 * "II" for little-endian (a.k.a. "Intel byte ordering" or "MM" for big-endian (a.k.a. "Motorola byte ordering" byte
 * ordering. I don't see where this is handled in the code.
 */
bool SipiIOTiff::read(SipiImage *img,
  const std::string &filepath,
  std::shared_ptr<SipiRegion> region,
  std::shared_ptr<SipiSize> size,
  bool force_bps_8,
  ScalingQuality scaling_quality)
{
  TIFF *tif;

  if (nullptr != (tif = TIFFOpen(filepath.c_str(), "r"))) {
    TIFFSetErrorHandler(tiffError);
    TIFFSetWarningHandler(tiffWarning);
    TIFFSetField(tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);

    //
    // OK, it's a TIFF file
    //
    uint16_t safo, ori, planar, stmp;

    (void)TIFFSetWarningHandler(nullptr);

    if (TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &(img->nx)) == 0) {
      TIFFClose(tif);
      std::string msg = "TIFFGetField of TIFFTAG_IMAGEWIDTH failed: " + filepath;
      throw Sipi::SipiImageError(msg);
    }

    if (TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &(img->ny)) == 0) {
      TIFFClose(tif);
      std::string msg = "TIFFGetField of TIFFTAG_IMAGELENGTH failed: " + filepath;
      throw Sipi::SipiImageError(msg);
    }

    TIFF_GET_FIELD(tif, TIFFTAG_SAMPLESPERPIXEL, &stmp, 1);
    img->nc = static_cast<size_t>(stmp);

    TIFF_GET_FIELD(tif, TIFFTAG_BITSPERSAMPLE, &stmp, 1);
    img->bps = static_cast<size_t>(stmp);

    TIFF_GET_FIELD(tif, TIFFTAG_ORIENTATION, &ori, ORIENTATION_TOPLEFT);
    img->orientation = static_cast<Orientation>(ori);

    if (1 != TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &stmp)) {
      img->photo = PhotometricInterpretation::MINISBLACK;
    } else {
      img->photo = static_cast<PhotometricInterpretation>(stmp);
    }

    //
    // if we have a palette TIFF with a colormap, it gets complicated. We will have to
    // read the colormap and later convert the image to RGB, since we do internally
    // not support palette images.
    //


    std::vector<uint16_t> rcm;
    std::vector<uint16_t> gcm;
    std::vector<uint16_t> bcm;

    int colmap_len = 0;
    if (img->photo == PhotometricInterpretation::PALETTE) {
      uint16_t *_rcm = nullptr, *_gcm = nullptr, *_bcm = nullptr;
      if (TIFFGetField(tif, TIFFTAG_COLORMAP, &_rcm, &_gcm, &_bcm) == 0) {
        TIFFClose(tif);
        std::string msg = "TIFFGetField of TIFFTAG_COLORMAP failed: " + filepath;
        throw Sipi::SipiImageError(msg);
      }
      colmap_len = 1;
      size_t itmp = 0;
      while (itmp < img->bps) {
        colmap_len *= 2;
        itmp++;
      }
      rcm.reserve(colmap_len);
      gcm.reserve(colmap_len);
      bcm.reserve(colmap_len);
      for (int ii = 0; ii < colmap_len; ii++) {
        rcm[ii] = _rcm[ii];
        gcm[ii] = _gcm[ii];
        bcm[ii] = _bcm[ii];
      }
    }

    TIFF_GET_FIELD(tif, TIFFTAG_PLANARCONFIG, &planar, PLANARCONFIG_CONTIG);
    TIFF_GET_FIELD(tif, TIFFTAG_SAMPLEFORMAT, &safo, SAMPLEFORMAT_UINT);

    uint16_t *es;
    int eslen = 0;
    if (TIFFGetField(tif, TIFFTAG_EXTRASAMPLES, &eslen, &es) == 1) {
      for (int i = 0; i < eslen; i++) {
        ExtraSamples extra;
        switch (es[i]) {
        case 0:
          extra = ExtraSamples::UNSPECIFIED;
          break;
        case 1:
          extra = ExtraSamples::ASSOCALPHA;
          break;
        case 2:
          extra = ExtraSamples::UNASSALPHA;
          break;
        default:
          extra = ExtraSamples::UNSPECIFIED;
        }
        img->es.push_back(extra);
      }
    }

    //
    // reading TIFF Meatdata and adding the fields to the exif header.
    // We store the TIFF metadata in the private exifData member variable using addKeyVal.
    //

    char *str;

    if (1 == TIFFGetField(tif, TIFFTAG_IMAGEDESCRIPTION, &str)) {
      img->ensure_exif();
      img->exif->addKeyVal(std::string("Exif.Image.ImageDescription"), std::string(str));
    }
    if (1 == TIFFGetField(tif, TIFFTAG_MAKE, &str)) {
      img->ensure_exif();
      img->exif->addKeyVal(std::string("Exif.Image.Make"), std::string(str));
    }
    if (1 == TIFFGetField(tif, TIFFTAG_MODEL, &str)) {
      img->ensure_exif();
      img->exif->addKeyVal(std::string("Exif.Image.Model"), std::string(str));
    }
    if (1 == TIFFGetField(tif, TIFFTAG_SOFTWARE, &str)) {
      img->ensure_exif();
      img->exif->addKeyVal(std::string("Exif.Image.Software"), std::string(str));
    }
    if (1 == TIFFGetField(tif, TIFFTAG_DATETIME, &str)) {
      img->ensure_exif();
      img->exif->addKeyVal(std::string("Exif.Image.DateTime"), std::string(str));
    }
    if (1 == TIFFGetField(tif, TIFFTAG_ARTIST, &str)) {
      img->ensure_exif();
      img->exif->addKeyVal(std::string("Exif.Image.Artist"), std::string(str));
    }
    if (1 == TIFFGetField(tif, TIFFTAG_HOSTCOMPUTER, &str)) {
      img->ensure_exif();
      img->exif->addKeyVal(std::string("Exif.Image.HostComputer"), std::string(str));
    }
    if (1 == TIFFGetField(tif, TIFFTAG_COPYRIGHT, &str)) {
      img->ensure_exif();
      img->exif->addKeyVal(std::string("Exif.Image.Copyright"), std::string(str));
    }
    if (1 == TIFFGetField(tif, TIFFTAG_DOCUMENTNAME, &str)) {
      img->ensure_exif();
      img->exif->addKeyVal(std::string("Exif.Image.DocumentName"), std::string(str));
    }
    // ???????? What shall we do with this meta data which is not standard in exif??????
    // We could add it as Xmp?
    //
    /*
                if (1 == TIFFGetField(tif, TIFFTAG_PAGENAME, &str)) {
                    if (img->exif == NULL) img->exif = std::make_shared<SipiExif>();
                    img->exif->addKeyVal(string("Exif.Image.PageName"), string(str));
                }
                if (1 == TIFFGetField(tif, TIFFTAG_PAGENUMBER, &str)) {
                    if (img->exif == NULL) img->exif = std::make_shared<SipiExif>();
                    img->exif->addKeyVal(string("Exif.Image.PageNumber"), string(str));
                }
    */
    float f;
    if (1 == TIFFGetField(tif, TIFFTAG_XRESOLUTION, &f)) {
      img->ensure_exif();
      img->exif->addKeyVal(std::string("Exif.Image.XResolution"), SipiExif::toRational(f));
    }
    if (1 == TIFFGetField(tif, TIFFTAG_YRESOLUTION, &f)) {
      img->ensure_exif();
      img->exif->addKeyVal(std::string("Exif.Image.YResolution"), SipiExif::toRational(f));
    }

    short s;
    if (1 == TIFFGetField(tif, TIFFTAG_RESOLUTIONUNIT, &s)) {
      img->ensure_exif();
      img->exif->addKeyVal(std::string("Exif.Image.ResolutionUnit"), s);
    }

    //
    // read iptc header
    //
    unsigned int iptc_length = 0;
    unsigned char *iptc_content = nullptr;

    if (TIFFGetField(tif, TIFFTAG_RICHTIFFIPTC, &iptc_length, &iptc_content) != 0) {
      try {
        img->iptc = std::make_shared<SipiIptc>(iptc_content, iptc_length);
      } catch (SipiError &err) {
        log_err("%s", err.to_string().c_str());
      }
    }

    //
    // read exif here....
    //
    toff_t exif_ifd_offs;
    if (1 == TIFFGetField(tif, TIFFTAG_EXIFIFD, &exif_ifd_offs)) {
      img->ensure_exif();
      readExif(img, tif, exif_ifd_offs);
    }

    //
    // read xmp header
    //
    int xmp_length;
    char *xmp_content = nullptr;

    if (1 == TIFFGetField(tif, TIFFTAG_XMLPACKET, &xmp_length, &xmp_content)) {
      try {
        img->xmp = std::make_shared<SipiXmp>(xmp_content, xmp_length);
      } catch (SipiError &err) {
        log_err("%s", err.to_string().c_str());
      }
    }

    //
    // Read ICC-profile
    //
    unsigned int icc_len;
    unsigned char *icc_buf;
    float *whitepoint_ti = nullptr;
    float whitepoint[2];

    if (1 == TIFFGetField(tif, TIFFTAG_ICCPROFILE, &icc_len, &icc_buf)) {
      try {
        img->icc = std::make_shared<SipiIcc>(icc_buf, icc_len);
      } catch (SipiError &err) {
        log_err("%s", err.to_string().c_str());
      }
    } else if (1 == TIFFGetField(tif, TIFFTAG_WHITEPOINT, &whitepoint_ti)) {
      whitepoint[0] = whitepoint_ti[0];
      whitepoint[1] = whitepoint_ti[1];
      //
      // Wow, we have TIFF colormetry..... Who is still using this???
      //
      float *primaries_ti = nullptr;
      float primaries[6];

      if (1 == TIFFGetField(tif, TIFFTAG_PRIMARYCHROMATICITIES, &primaries_ti)) {
        primaries[0] = primaries_ti[0];
        primaries[1] = primaries_ti[1];
        primaries[2] = primaries_ti[2];
        primaries[3] = primaries_ti[3];
        primaries[4] = primaries_ti[4];
        primaries[5] = primaries_ti[5];
      } else {
        //
        // not defined, let's take the sRGB primaries
        //
        primaries[0] = 0.6400;
        primaries[1] = 0.3300;
        primaries[2] = 0.3000;
        primaries[3] = 0.6000;
        primaries[4] = 0.1500;
        primaries[5] = 0.0600;
      }

      unsigned short *tfunc = new unsigned short[3 * (1 << img->bps)], *tfunc_ti;
      unsigned int tfunc_len, tfunc_len_ti;

      if (1 == TIFFGetField(tif, TIFFTAG_TRANSFERFUNCTION, &tfunc_len_ti, &tfunc_ti)) {
        if ((tfunc_len_ti / (1 << img->bps)) == 1) {
          memcpy(tfunc, tfunc_ti, tfunc_len_ti);
          memcpy(tfunc + tfunc_len_ti, tfunc_ti, tfunc_len_ti);
          memcpy(tfunc + 2 * tfunc_len_ti, tfunc_ti, tfunc_len_ti);
          tfunc_len = tfunc_len_ti;
        } else {
          memcpy(tfunc, tfunc_ti, tfunc_len_ti);
          tfunc_len = tfunc_len_ti / 3;
        }
      } else {
        delete[] tfunc;
        tfunc = nullptr;
        tfunc_len = 0;
      }

      img->icc = std::make_shared<SipiIcc>(whitepoint, primaries, tfunc, tfunc_len);
      delete[] tfunc;
    }

    //
    // Read SipiEssential metadata
    //
    char *emdatastr = nullptr;

    if (1 == TIFFGetField(tif, TIFFTAG_SIPIMETA, &emdatastr)) {
      if (strlen(emdatastr) > 0) {
        SipiEssentials se(emdatastr);
        img->essential_metadata(se);
      }
    }

    // TODO: the TIFFSetDirectory(tif, 0); in read_resolutions introduces a regression for JPEG auto-conversion test
    auto resolutions = read_resolutions(img->getNx(), tif);
    int reduce = -1;

    size_t w = img->nx, h = img->ny;
    size_t out_w, out_h;
    bool redonly;
    bool is_tiled;
    uint32_t level = 0;

    if (size) {
      size->get_size(w, h, out_w, out_h, reduce, redonly);

      // NOTE: if uncommented, region + pct:50 will not behave
      // level = -1;
      // for (auto r : resolutions) {
      //   printf("[SipiIOTiff] resolution(%i) // %i > %i == %i\n", level, r.reduce, reduce, r.reduce > reduce);
      //   ++level;
      //   if (r.reduce > reduce) break;
      // }
      // printf("[SipiIOTiff] resolution level picked: %i\n", level);
      // TIFFSetDirectory(tif, level);

      img->nx = resolutions[level].width;
      img->ny = resolutions[level].height;
      if (region != nullptr) { region->set_reduce(static_cast<float>(reduce)); }
    }
    is_tiled = (resolutions[level].tile_width != 0) && (resolutions[level].tile_height != 0);

    auto sll = static_cast<uint32_t>(TIFFScanlineSize(tif));

    int32_t roi_x;
    int32_t roi_y;
    size_t roi_w;
    size_t roi_h;
    if (region == nullptr) {
      roi_x = 0;
      roi_y = 0;
      roi_w = img->nx;
      roi_h = img->ny;
    } else {
      region->crop_coords(img->nx, img->ny, roi_x, roi_y, roi_w, roi_h);
    }

    int ps;// pixel size in bytes
    switch (img->bps) {
    case 1: {
      std::string msg = "Images with 1 bit/sample not supported in file " + filepath;
      throw Sipi::SipiImageError(msg);
    }

    case 8:
      ps = 1;
    case 16:
      ps = 2;
    }

    auto *inbuf = new uint8_t[ps * roi_w * roi_h * img->nc];

    if (img->bps <= 8) {
      std::vector<uint8_t> pixdata;
      if (is_tiled)
        pixdata = read_tiled_data<uint8_t>(tif, roi_x, roi_y, roi_w, roi_h);
      else
        pixdata = read_standard_data<uint8_t>(tif, roi_x, roi_y, roi_w, roi_h);

      img->bps = 8;

      memcpy(inbuf, pixdata.data(), pixdata.size() * img->bps / 8);
    } else if (img->bps <= 16) {
      std::vector<uint16_t> pixdata;
      if (is_tiled)
        pixdata = read_tiled_data<uint16_t>(tif, roi_x, roi_y, roi_w, roi_h);
      else
        pixdata = read_standard_data<uint16_t>(tif, roi_x, roi_y, roi_w, roi_h);
      img->bps = 16;
      memcpy(inbuf, pixdata.data(), pixdata.size() * img->bps / 8);
    }

    img->pixels = inbuf;
    img->nx = roi_w;
    img->ny = roi_h;
    TIFFClose(tif);

    if (img->photo == PhotometricInterpretation::PALETTE) {
      //
      // ok, we have a palette color image we have to convert to RGB...
      //
      uint16_t cm_max = 0;
      for (int i = 0; i < colmap_len; i++) {
        if (rcm[i] > cm_max) cm_max = rcm[i];
        if (gcm[i] > cm_max) cm_max = gcm[i];
        if (bcm[i] > cm_max) cm_max = bcm[i];
      }
      auto *dataptr = new uint8_t[3 * img->nx * img->ny];
      if (cm_max <= 256) {// we have a colomap with entries form 0 - 255
        for (size_t i = 0; i < img->nx * img->ny; i++) {
          dataptr[3 * i] = (uint8_t)rcm[img->pixels[i]];
          dataptr[3 * i + 1] = (uint8_t)gcm[img->pixels[i]];
          dataptr[3 * i + 2] = (uint8_t)bcm[img->pixels[i]];
        }
      } else {// we have a colormap with entries > 255, assuming 16 bit
        for (size_t i = 0; i < img->nx * img->ny; i++) {
          dataptr[3 * i] = (uint8_t)(rcm[img->pixels[i]] >> 8);
          dataptr[3 * i + 1] = (uint8_t)(gcm[img->pixels[i]] >> 8);
          dataptr[3 * i + 2] = (uint8_t)(bcm[img->pixels[i]] >> 8);
        }
      }
      delete[] img->pixels;
      img->pixels = dataptr;
      dataptr = nullptr;
      img->photo = PhotometricInterpretation::RGB;
      img->nc = 3;
    }

    if (img->icc == nullptr) {
      switch (img->photo) {
      case PhotometricInterpretation::MINISBLACK: {
        if (img->bps == 1) { cvrt1BitTo8Bit(img, sll, 0, 255); }
        img->icc = std::make_shared<SipiIcc>(icc_GRAY_D50);
        break;
      }

      case PhotometricInterpretation::MINISWHITE: {
        if (img->bps == 1) { cvrt1BitTo8Bit(img, sll, 255, 0); }
        img->icc = std::make_shared<SipiIcc>(icc_GRAY_D50);
        break;
      }

      case PhotometricInterpretation::SEPARATED: {
        img->icc = std::make_shared<SipiIcc>(icc_CMYK_standard);
        break;
      }

      case PhotometricInterpretation::YCBCR:// fall through!

      case PhotometricInterpretation::RGB: {
        img->icc = std::make_shared<SipiIcc>(icc_sRGB);
        break;
      }

      case PhotometricInterpretation::CIELAB: {
        //
        // we have to convert to JPEG2000/littleCMS standard
        //
        if (img->bps == 8) {
          for (size_t y = 0; y < img->ny; y++) {
            for (size_t x = 0; x < img->nx; x++) {
              union {
                unsigned char u;
                signed char s;
              } v{};
              v.u = img->pixels[img->nc * (y * img->nx + x) + 1];
              img->pixels[img->nc * (y * img->nx + x) + 1] = 128 + v.s;
              v.u = img->pixels[img->nc * (y * img->nx + x) + 2];
              img->pixels[img->nc * (y * img->nx + x) + 2] = 128 + v.s;
            }
          }
          img->icc = std::make_shared<SipiIcc>(icc_LAB);
        } else if (img->bps == 16) {
          auto *data = (unsigned short *)img->pixels;
          for (size_t y = 0; y < img->ny; y++) {
            for (size_t x = 0; x < img->nx; x++) {
              union {
                unsigned short u;
                signed short s;
              } v{};
              v.u = data[img->nc * (y * img->nx + x) + 1];
              data[img->nc * (y * img->nx + x) + 1] = 32768 + v.s;
              v.u = data[img->nc * (y * img->nx + x) + 2];
              data[img->nc * (y * img->nx + x) + 2] = 32768 + v.s;
            }
          }
          img->icc = std::make_shared<SipiIcc>(icc_LAB);
        } else {
          throw Sipi::SipiImageError("Unsupported bits per sample (" + std::to_string(img->bps) + ")");
        }
        break;
      }

      default: {
        throw Sipi::SipiImageError("Unsupported photometric interpretation (" + to_string(img->photo) + ")");
      }
      }
    }
    /*
    if ((img->nc == 3) && (img->photo == PHOTOMETRIC_YCBCR)) {
        std::shared_ptr<SipiIcc> target_profile = std::make_shared<SipiIcc>(img->icc);
        switch (img->bps) {
            case 8: {
                img->convertToIcc(target_profile, TYPE_YCbCr_8);
                break;
            }
            case 16: {
                img->convertToIcc(target_profile, TYPE_YCbCr_16);
                break;
            }
            default: {
                throw Sipi::SipiImageError(thisSourceFile, __LINE__, "Unsupported bits/sample (" + std::to_string(bps) +
    ")!");
            }
        }
    }
    else if ((img->nc == 4) && (img->photo == PHOTOMETRIC_SEPARATED)) { // CMYK image
        std::shared_ptr<SipiIcc> target_profile = std::make_shared<SipiIcc>(icc_sRGB);
        switch (img->bps) {
            case 8: {
                img->convertToIcc(target_profile, TYPE_CMYK_8);
                break;
            }
            case 16: {
                img->convertToIcc(target_profile, TYPE_CMYK_16);
                break;
            }
            default: {
                throw Sipi::SipiImageError(thisSourceFile, __LINE__, "Unsupported bits/sample (" + std::to_string(bps) +
    ")!");
            }
        }
    }
    */
    //
    // resize/Scale the image if necessary
    //
    if (size != NULL) {
      size_t nnx, nny;
      int reduce = -1;
      bool redonly;
      SipiSize::SizeType rtype = size->get_size(w, h, nnx, nny, reduce, redonly);
      if (rtype != SipiSize::FULL) {
        switch (scaling_quality.jpeg) {
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
    if (force_bps_8) {
      if (!img->to8bps()) { throw Sipi::SipiImageError("Cannont convert to 8 Bits(sample"); }
    }
    return true;
  }
  return false;
}
//============================================================================


SipiImgInfo SipiIOTiff::getDim(const std::string &filepath)
{
  TIFF *tif;
  SipiImgInfo info;
  if (nullptr != (tif = TIFFOpen(filepath.c_str(), "r"))) {
    //
    // OK, it's a TIFF file
    //
    (void)TIFFSetWarningHandler(nullptr);
    unsigned int tmp_width;

    if (TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &tmp_width) == 0) {
      TIFFClose(tif);
      std::string msg = "TIFFGetField of TIFFTAG_IMAGEWIDTH failed: " + filepath;
      throw Sipi::SipiImageError(msg);
    }

    info.width = (size_t)tmp_width;
    unsigned int tmp_height;

    if (TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &tmp_height) == 0) {
      TIFFClose(tif);
      std::string msg = "TIFFGetField of TIFFTAG_IMAGELENGTH failed: " + filepath;
      throw Sipi::SipiImageError(msg);
    }
    info.height = tmp_height;
    info.success = SipiImgInfo::DIMS;

    unsigned short ori;
    TIFF_GET_FIELD(tif, TIFFTAG_ORIENTATION, &ori, ORIENTATION_TOPLEFT);
    info.orientation = static_cast<Orientation>(ori);

    char *emdatastr;
    if (1 == TIFFGetField(tif, TIFFTAG_SIPIMETA, &emdatastr)) {
      SipiEssentials se(emdatastr);
      info.origmimetype = se.mimetype();
      info.origname = se.origname();
      info.success = SipiImgInfo::ALL;
    }

    info.resolutions = read_resolutions(tmp_width, tif);

    TIFFClose(tif);
  }
  return info;
}
//============================================================================

void SipiIOTiff::write_basic_tags(const SipiImage &img,
  TIFF *tif,
  uint32_t nx,
  uint32_t ny,
  bool its_1_bit,
  const std::string &compression)
{
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(nx));
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(ny));
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  if (its_1_bit) {
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, (uint16_t)1);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX4);// that's out default....
  } else {
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(img.bps));
    if (compression == "COMPRESSION_LZW") {
      TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
    } else if (compression == "COMPRESSION_DEFLATE") {
      TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_DEFLATE);
    }
  }
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, img.nc);
  if (!img.es.empty()) { TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, img.es.size(), img.es.data()); }
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, img.photo);
}

void SipiIOTiff::write(SipiImage *img, const std::string &filepath, const SipiCompressionParams *params)
{
  TIFF *tif;
  MEMTIFF *memtif = nullptr;
  auto rowsperstrip = (uint32_t)-1;
  if ((filepath == "stdout:") || (filepath == "HTTP")) {
    memtif = memTiffOpen();
    tif = TIFFClientOpen("MEMTIFF",
      "w",
      (thandle_t)memtif,
      memTiffReadProc,
      memTiffWriteProc,
      memTiffSeekProc,
      memTiffCloseProc,
      memTiffSizeProc,
      memTiffMapProc,
      memTiffUnmapProc);
  } else {
    if ((tif = TIFFOpen(filepath.c_str(), "w")) == nullptr) {
      if (memtif != nullptr) memTiffFree(memtif);
      std::string msg = "TIFFopen of \"" + filepath + "\" failed!";
      throw Sipi::SipiImageError(msg);
    }
  }
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<int>(img->nx));
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<int>(img->ny));
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, rowsperstrip));
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  bool its_1_bit = false;
  if ((img->photo == PhotometricInterpretation::MINISWHITE) || (img->photo == PhotometricInterpretation::MINISBLACK)) {
    its_1_bit = true;

    if (img->bps == 8) {
      byte *scan = img->pixels;
      for (size_t i = 0; i < img->nx * img->ny; i++) {
        if ((scan[i] != 0) && (scan[i] != 255)) { its_1_bit = false; }
      }
    } else if (img->bps == 16) {
      word *scan = (word *)img->pixels;
      for (size_t i = 0; i < img->nx * img->ny; i++) {
        if ((scan[i] != 0) && (scan[i] != 65535)) { its_1_bit = false; }
      }
    }

    if (its_1_bit) {
      TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, (uint16_t)1);
      TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX4);// that's out default....
    } else {
      TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, (uint16_t)img->bps);
    }
  } else {
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, (uint16_t)img->bps);
  }
  if (img->photo == PhotometricInterpretation::CIELAB) {
    if (img->bps == 8) {
      for (size_t y = 0; y < img->ny; y++) {
        for (size_t x = 0; x < img->nx; x++) {
          union {
            unsigned char u;
            signed char s;
          } v{};
          v.s = img->pixels[img->nc * (y * img->nx + x) + 1] - 128;
          img->pixels[img->nc * (y * img->nx + x) + 1] = v.u;
          v.s = img->pixels[img->nc * (y * img->nx + x) + 2] - 128;
          img->pixels[img->nc * (y * img->nx + x) + 2] = v.u;
        }
      }
    } else if (img->bps == 16) {
      for (size_t y = 0; y < img->ny; y++) {
        for (size_t x = 0; x < img->nx; x++) {
          auto *data = (unsigned short *)img->pixels;
          union {
            unsigned short u;
            signed short s;
          } v{};
          v.s = data[img->nc * (y * img->nx + x) + 1] - 32768;
          data[img->nc * (y * img->nx + x) + 1] = v.u;
          v.s = data[img->nc * (y * img->nx + x) + 2] - 32768;
          data[img->nc * (y * img->nx + x) + 2] = v.u;
        }
      }
    } else {
      throw Sipi::SipiImageError("Unsupported bits per sample (" + std::to_string(img->bps) + ")");
    }

    // delete img->icc; we don't want to add the ICC profile in this case (doesn't make sense!)
    img->icc = nullptr;
  }
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, img->nc);

  if (img->es.size() > 0) { TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, img->es.size(), img->es.data()); }

  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, img->photo);

  //
  // let's get the TIFF metadata if there is some. We stored the TIFF metadata in the exifData member variable!
  //
  if ((img->exif != nullptr) & static_cast<int>(!(img->skip_metadata & SkipMetadata::SKIP_EXIF))) {
    std::string value;

    if (img->exif->getValByKey("Exif.Image.ImageDescription", value)) {
      TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, value.c_str());
    }

    if (img->exif->getValByKey("Exif.Image.Make", value)) { TIFFSetField(tif, TIFFTAG_MAKE, value.c_str()); }

    if (img->exif->getValByKey("Exif.Image.Model", value)) { TIFFSetField(tif, TIFFTAG_MODEL, value.c_str()); }

    if (img->exif->getValByKey("Exif.Image.Software", value)) { TIFFSetField(tif, TIFFTAG_SOFTWARE, value.c_str()); }

    if (img->exif->getValByKey("Exif.Image.DateTime", value)) { TIFFSetField(tif, TIFFTAG_DATETIME, value.c_str()); }

    if (img->exif->getValByKey("Exif.Image.Artist", value)) { TIFFSetField(tif, TIFFTAG_ARTIST, value.c_str()); }

    if (img->exif->getValByKey("Exif.Image.HostComputer", value)) {
      TIFFSetField(tif, TIFFTAG_HOSTCOMPUTER, value.c_str());
    }

    if (img->exif->getValByKey("Exif.Image.Copyright", value)) { TIFFSetField(tif, TIFFTAG_COPYRIGHT, value.c_str()); }

    if (img->exif->getValByKey("Exif.Image.DocumentName", value)) {
      TIFFSetField(tif, TIFFTAG_DOCUMENTNAME, value.c_str());
    }

    float f;

    if (img->exif->getValByKey("Exif.Image.XResolution", f)) { TIFFSetField(tif, TIFFTAG_XRESOLUTION, f); }

    if (img->exif->getValByKey("Exif.Image.YResolution", f)) { TIFFSetField(tif, TIFFTAG_XRESOLUTION, f); }

    short s;

    if (img->exif->getValByKey("Exif.Image.ResolutionUnit", s)) { TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, s); }
  }
  SipiEssentials es = img->essential_metadata();
  if (((img->icc != nullptr) || es.use_icc()) & (!(img->skip_metadata & SKIP_ICC))) {
    std::vector<unsigned char> buf;
    try {
      if (es.use_icc()) {
        buf = es.icc_profile();
      } else {
        buf = img->icc->iccBytes();
      }

      if (buf.size() > 0) { TIFFSetField(tif, TIFFTAG_ICCPROFILE, buf.size(), buf.data()); }
    } catch (SipiError &err) {
      log_err("%s", err.to_string().c_str());
    }
  }
  //
  // write IPTC data, if available
  //
  if ((img->iptc != nullptr) & (!(img->skip_metadata & SKIP_IPTC))) {
    try {
      std::vector<unsigned char> buf = img->iptc->iptcBytes();
      if (buf.size() > 0) { TIFFSetField(tif, TIFFTAG_RICHTIFFIPTC, buf.size(), buf.data()); }
    } catch (SipiError &err) {
      log_err("%s", err.to_string().c_str());
    }
  }

  //
  // write XMP data
  //
  if ((img->xmp != nullptr) & (!(img->skip_metadata & SKIP_XMP))) {
    try {
      std::string buf = img->xmp->xmpBytes();
      if (!buf.empty() > 0) { TIFFSetField(tif, TIFFTAG_XMLPACKET, buf.size(), buf.c_str()); }
    } catch (SipiError &err) {
      log_err("%s", err.to_string().c_str());
    }
  }
  //
  // Custom tag for SipiEssential metadata
  //
  if (es.is_set()) {
    std::string emdata = es;
    TIFFSetField(tif, TIFFTAG_SIPIMETA, emdata.c_str());
  }
  // TIFFCheckpointDirectory(tif);
  if (its_1_bit) {
    unsigned int sll;
    unsigned char *buf = cvrt8BitTo1bit(*img, sll);

    for (size_t i = 0; i < img->ny; i++) { TIFFWriteScanline(tif, buf + i * sll, (int)i, 0); }

    delete[] buf;
  } else {
    bool pyramid = false;

    if (params && params->contains(TIFF_Pyramid)) { pyramid = params->at(TIFF_Pyramid).compare("yes") == 0; }

    if (!pyramid) {
      for (size_t i = 0; i < img->ny; i++) {
        TIFFWriteScanline(tif, img->pixels + i * img->nc * img->nx * (img->bps / 8), (int)i, 0);
      }
    } else {
      for (int reduce = 0; reduce <= 5; reduce += 1) {
        SipiSize size(reduce);
        size_t nnx;
        size_t nny;
        bool redonly;
        (void)size.get_size(img->nx, img->ny, nnx, nny, reduce, redonly);

        if (std::min(nnx, nny) <= 32) break;

        uint32_t tw = 0, th = tw;
        write_subfile(*img, tif, reduce, tw, th, "");
      }
    }
  }

  //
  // write exif data
  //
  if (img->exif != nullptr) {
    TIFFWriteDirectory(tif);
    writeExif(img, tif);
  }
  TIFFClose(tif);

  if (memtif != nullptr) {
    if (filepath == "stdout:") {
      size_t n = 0;

      while (n < memtif->flen) {
        n += fwrite(&(memtif->data[n]), 1, memtif->flen - n > 10240 ? 10240 : memtif->flen - n, stdout);
      }

      fflush(stdout);
    } else if (filepath == "HTTP") {
      try {
        img->connection()->sendAndFlush(memtif->data, memtif->flen);
      } catch (int i) {
        memTiffFree(memtif);
        throw Sipi::SipiImageError("Sending data failed! Broken pipe?: " + filepath + " !");
      }
    } else {
      memTiffFree(memtif);
      throw Sipi::SipiImageError("Unknown output method: " + filepath + " !");
    }

    memTiffFree(memtif);
  }
}
//============================================================================

template<typename T>
std::vector<T> doReduce(std::vector<T> &&inbuf, int reduce, size_t nx, size_t ny, size_t nc, size_t &nnx, size_t nny)
{
  SipiSize size(reduce);
  int r = 0;
  bool redonly;
  size.get_size(nx, ny, nnx, nny, r, redonly);
  auto outbuf = std::vector<T>(nnx * nny * nc);

  auto inbuf_raw = inbuf.data();
  auto outbuf_raw = outbuf.data();
  reduce = 1 << reduce;
  if (reduce <= 1) {
    memcpy(outbuf_raw, inbuf_raw, nnx * nny * nc * sizeof(T));
  } else {
    for (uint32_t y = 0; y < nny; ++y) {
      for (uint32_t x = 0; x < nnx; ++x) {
        for (uint32_t c = 0; c < nc; ++c) {
          uint32_t cnt = 0;
          uint32_t tmp = 0;
          for (uint32_t xx = 0; xx < reduce; ++xx) {
            for (uint32_t yy = 0; yy < reduce; ++yy) {
              if (((reduce * x + xx) < nx) && ((reduce * y + yy) < ny)) {
                tmp += static_cast<uint32_t>(inbuf[nc * ((reduce * y + yy) * nx + (reduce * x + xx)) + c]);
                ++cnt;
              }
            }
          }
          outbuf[nc * (y * nnx + x) + c] = tmp / cnt;
        }
      }
    }
  }
  return outbuf;
}

void SipiIOTiff::write_subfile(const SipiImage &img,
  TIFF *tif,
  int level,
  uint32_t &tile_width,
  uint32_t &tile_height,
  const std::string &compression)
{
  SipiSize size(level);
  size_t nnx;
  size_t nny;
  bool redonly;
  /* try { */
  (void)size.get_size(img.nx, img.ny, nnx, nny, level, redonly);
  /* } catch (const IIIFSizeError &err) {} */

  write_basic_tags(img, tif, nnx, nny, false, compression);
  if (level > 0) { TIFFSetField(tif, TIFFTAG_SUBFILETYPE, FILETYPE_REDUCEDIMAGE); }

  if (tile_width == 0 || tile_height == 0) { TIFFDefaultTileSize(tif, &tile_width, &tile_height); }
  TIFFSetField(tif, TIFFTAG_TILEWIDTH, tile_width);
  TIFFSetField(tif, TIFFTAG_TILELENGTH, tile_height);

  tsize_t tilesize = TIFFTileSize(tif);

  // reduce resolution of image: A reduce factor can be given, 0=no scaling, 1=0.5, 2=0.25, 3=0.125,...
  std::vector<uint8_t> nbuf(img.pixels, img.pixels + img.nx * img.ny * img.nc * img.bps / 8);

  if (level > 0) { nbuf = doReduce<uint8_t>(std::move(nbuf), level, img.nx, img.ny, img.nc, nnx, nny); }

  auto ntiles_x = static_cast<uint32_t>(ceilf(static_cast<float>(nnx) / static_cast<float>(tile_width)));
  auto ntiles_y = static_cast<uint32_t>(ceilf(static_cast<float>(nny) / static_cast<float>(tile_height)));

  auto tilebuf = std::vector<uint8_t>(tilesize);
  for (uint32_t ty = 0; ty < ntiles_y; ++ty) {
    for (uint32_t tx = 0; tx < ntiles_x; ++tx) {
      for (uint32_t y = 0; y < tile_height; ++y) {
        for (uint32_t x = 0; x < tile_width; ++x) {
          for (uint32_t c = 0; c < img.nc; ++c) {
            uint32_t xx = tx * tile_width + x;
            uint32_t yy = ty * tile_height + y;
            if ((xx < nnx) && (yy < nny)) {
              tilebuf[img.nc * (y * tile_width + x) + c] = nbuf[img.nc * (yy * nnx + xx) + c];
            } else {
              tilebuf[img.nc * (y * tile_width + x) + c] = 0;
            }
          }
        }
      }
      TIFFWriteTile(tif, static_cast<void *>(tilebuf.data()), tx * tile_width, ty * tile_height, 0, 0);
    }
  }
  TIFFWriteDirectory(tif);
}

void SipiIOTiff::readExif(SipiImage *img, TIFF *tif, toff_t exif_offset)
{
  uint16_t curdir = TIFFCurrentDirectory(tif);

  if (TIFFReadEXIFDirectory(tif, exif_offset)) {
    for (int i = 0; i < exiftag_list_len; i++) {
      switch (exiftag_list[i].datatype) {
      case EXIF_DT_RATIONAL: {
        float f;
        if (TIFFGetField(tif, exiftag_list[i].tag_id, &f)) {
          Exiv2::Rational r = SipiExif::toRational(f);
          try {
            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", r);
          } catch (const SipiError &err) {
            log_err("Error writing EXIF data: %s", err.to_string().c_str());
          }
        }
        break;
      }

      case EXIF_DT_UINT8: {
        unsigned char uc;
        if (TIFFGetField(tif, exiftag_list[i].tag_id, &uc)) {
          try {
            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", uc);
          } catch (const SipiError &err) {
            log_err("Error writing EXIF data: %s", err.to_string().c_str());
          }
        }
        break;
      }

      case EXIF_DT_UINT16: {
        unsigned short us;
        if (TIFFGetField(tif, exiftag_list[i].tag_id, &us)) {
          try {
            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", us);
          } catch (const SipiError &err) {
            log_err("Error writing EXIF data: %s", err.to_string().c_str());
          }
        }
        break;
      }

      case EXIF_DT_UINT32: {
        unsigned int ui;
        if (TIFFGetField(tif, exiftag_list[i].tag_id, &ui)) {
          try {
            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", ui);
          } catch (const SipiError &err) {
            log_err("Error writing EXIF data: %s", err.to_string().c_str());
          }
        }
        break;
      }

      case EXIF_DT_STRING: {
        char *tmpstr = nullptr;
        if (TIFFGetField(tif, exiftag_list[i].tag_id, &tmpstr)) {
          try {
            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", std::string(tmpstr));
          } catch (const SipiError &err) {
            log_err("Error writing EXIF data: %s", err.to_string().c_str());
          }
        }
        break;
      }

      case EXIF_DT_RATIONAL_PTR: {
        float *tmpbuf;
        uint16_t len;
        if (TIFFGetField(tif, exiftag_list[i].tag_id, &len, &tmpbuf)) {
          auto *r = new Exiv2::Rational[len];
          for (int i; i < len; i++) { r[i] = SipiExif::toRational(tmpbuf[i]); }
          try {
            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", r, len);
          } catch (const SipiError &err) {
            log_err("Error writing EXIF data: %s", err.to_string().c_str());
          }
          delete[] r;
        }
        break;
      }

      case EXIF_DT_UINT8_PTR: {
        uint8_t *tmpbuf;
        uint16_t len;

        if (TIFFGetField(tif, exiftag_list[i].tag_id, &len, &tmpbuf)) {
          try {
            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", tmpbuf, len);
          } catch (const SipiError &err) {
            log_err("Error writing EXIF data: %s", err.to_string().c_str());
          }
        }
        break;
      }

      case EXIF_DT_UINT16_PTR: {
        uint16_t *tmpbuf;
        uint16_t len;// in bytes !!
        if (TIFFGetField(tif, exiftag_list[i].tag_id, &len, &tmpbuf)) {
          try {
            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", tmpbuf, len);
          } catch (const SipiError &err) {
            log_err("Error writing EXIF data: %s", err.to_string().c_str());
          }
        }
        break;
      }

      case EXIF_DT_UINT32_PTR: {
        uint32_t *tmpbuf;
        uint16_t len;
        if (TIFFGetField(tif, exiftag_list[i].tag_id, &len, &tmpbuf)) {
          try {
            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", tmpbuf, len);
          } catch (const SipiError &err) {
            log_err("Error writing EXIF data: %s", err.to_string().c_str());
          }
        }
        break;
      }

      case EXIF_DT_PTR: {
        unsigned char *tmpbuf;
        uint16_t len;

        if (exiftag_list[i].len == 0) {
          if (TIFFGetField(tif, exiftag_list[i].tag_id, &len, &tmpbuf)) {
            try {
              img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", tmpbuf, len);
            } catch (const SipiError &err) {
              log_err("Error writing EXIF data: %s", err.to_string().c_str());
            }
          }
        } else {
          len = exiftag_list[i].len;
          if (TIFFGetField(tif, exiftag_list[i].tag_id, &tmpbuf)) {
            try {
              img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", tmpbuf, len);
            } catch (const SipiError &err) {
              log_err("Error writing EXIF data: %s", err.to_string().c_str());
            }
          }
        }
        break;
      }

      default: {
        // NO ACTION HERE At THE MOMENT...
      }
      }
    }
  }

  TIFFSetDirectory(tif, curdir);
}
//============================================================================


void SipiIOTiff::writeExif(SipiImage *img, TIFF *tif)
{
  // add EXIF tags to the set of tags that libtiff knows about
  // necessary if we want to set EXIFTAG_DATETIMEORIGINAL, for example
  // const TIFFFieldArray *exif_fields = _TIFFGetExifFields();
  //_TIFFMergeFields(tif, exif_fields->fields, exif_fields->count);


  TIFFCreateEXIFDirectory(tif);
  int count = 0;
  for (int i = 0; i < exiftag_list_len; i++) {
    switch (exiftag_list[i].datatype) {
    case EXIF_DT_RATIONAL: {
      Exiv2::Rational r;

      if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", r)) {
        float f = (float)r.first / (float)r.second;
        TIFFSetField(tif, exiftag_list[i].tag_id, f);
        count++;
      }

      break;
    }

    case EXIF_DT_UINT8: {
      uint8_t uc;

      if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", uc)) {
        TIFFSetField(tif, exiftag_list[i].tag_id, uc);
        count++;
      }

      break;
    }

    case EXIF_DT_UINT16: {
      uint16_t us;

      if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", us)) {
        TIFFSetField(tif, exiftag_list[i].tag_id, us);
        count++;
      }

      break;
    }

    case EXIF_DT_UINT32: {
      uint32_t ui;

      if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", ui)) {
        TIFFSetField(tif, exiftag_list[i].tag_id, ui);
        count++;
      }

      break;
    }

    case EXIF_DT_STRING: {
      std::string tmpstr;
      if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", tmpstr)) {
        TIFFSetField(tif, exiftag_list[i].tag_id, tmpstr.c_str());
        count++;
      }

      break;
    }

    case EXIF_DT_RATIONAL_PTR: {
      std::vector<Exiv2::Rational> vr;

      if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", vr)) {
        int len = vr.size();
        float *f = new float[len];

        for (int i = 0; i < len; i++) {
          f[i] = (float)vr[i].first / (float)vr[i].second;//!!!!!!!!!!!!!!!!!!!!!!!!!
        }

        TIFFSetField(tif, exiftag_list[i].tag_id, len, f);
        delete[] f;
        count++;
      }
      break;
    }

    case EXIF_DT_UINT8_PTR: {
      std::vector<uint8_t> vuc;

      if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", vuc)) {
        int len = vuc.size();
        TIFFSetField(tif, exiftag_list[i].tag_id, len, vuc.data());
        count++;
      }

      break;
    }

    case EXIF_DT_UINT16_PTR: {
      std::vector<uint16_t> vus;
      if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", vus)) {
        int len = vus.size();
        TIFFSetField(tif, exiftag_list[i].tag_id, len, vus.data());
        count++;
      }
      break;
    }

    case EXIF_DT_UINT32_PTR: {
      std::vector<uint32_t> vui;

      if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", vui)) {
        int len = vui.size();
        TIFFSetField(tif, exiftag_list[i].tag_id, len, vui.data());
        count++;
      }

      break;
    }

    case EXIF_DT_PTR: {
      std::vector<unsigned char> vuc;

      if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", vuc)) {
        int len = vuc.size();
        TIFFSetField(tif, exiftag_list[i].tag_id, len, vuc.data());
        count++;
      }

      break;
    }

    default: {
      // NO ACTION HERE AT THE MOMENT...
    }
    }
  }

  if (count > 0) {
    uint64_t exif_dir_offset = 0L;
    TIFFWriteCustomDirectory(tif, &exif_dir_offset);
    TIFFSetDirectory(tif, 0);
    TIFFSetField(tif, TIFFTAG_EXIFIFD, exif_dir_offset);
  }
  // TIFFCheckpointDirectory(tif);
}
//============================================================================

void SipiIOTiff::separateToContig(SipiImage *img, unsigned int sll)
{
  //
  // rearrange RRRRRR...GGGGG...BBBBB data  to RGBRGBRGB…RGB
  //
  if (img->bps == 8) {
    byte *dataptr = img->pixels;
    auto *tmpptr = new unsigned char[img->nc * img->ny * img->nx];

    for (unsigned int k = 0; k < img->nc; k++) {
      for (unsigned int j = 0; j < img->ny; j++) {
        for (unsigned int i = 0; i < img->nx; i++) {
          tmpptr[img->nc * (j * img->nx + i) + k] = dataptr[k * img->ny * sll + j * img->nx + i];
        }
      }
    }

    delete[] dataptr;
    img->pixels = tmpptr;
  } else if (img->bps == 16) {
    word *dataptr = (word *)img->pixels;
    word *tmpptr = new word[img->nc * img->ny * img->nx];

    for (unsigned int k = 0; k < img->nc; k++) {
      for (unsigned int j = 0; j < img->ny; j++) {
        for (unsigned int i = 0; i < img->nx; i++) {
          tmpptr[img->nc * (j * img->nx + i) + k] = dataptr[k * img->ny * sll + j * img->nx + i];
        }
      }
    }

    delete[] dataptr;
    img->pixels = (byte *)tmpptr;
  } else {
    std::string msg = "Bits per sample not supported: " + std::to_string(-img->bps);
    throw Sipi::SipiImageError(msg);
  }
}
//============================================================================


void SipiIOTiff::cvrt1BitTo8Bit(SipiImage *img, unsigned int sll, unsigned int black, unsigned int white)
{
  byte *inbuf = img->pixels;
  byte *outbuf;
  byte *in_byte, *out_byte, *in_off, *out_off, *inbuf_high;

  static unsigned char mask[8] = { 128, 64, 32, 16, 8, 4, 2, 1 };
  unsigned int x, y, k;

  if ((img->photo != PhotometricInterpretation::MINISWHITE) && (img->photo != PhotometricInterpretation::MINISBLACK)) {
    throw Sipi::SipiImageError("Photometric interpretation is not MINISWHITE or  MINISBLACK");
  }

  if (img->bps != 1) {
    std::string msg = "Bits per sample is not 1 but: " + std::to_string(img->bps);
    throw Sipi::SipiImageError(msg);
  }

  outbuf = new byte[img->nx * img->ny];
  inbuf_high = inbuf + img->ny * sll;

  if ((8 * sll) == img->nx) {
    in_byte = inbuf;
    out_byte = outbuf;

    for (; in_byte < inbuf_high; in_byte++, out_byte += 8) {
      for (k = 0; k < 8; k++) { *(out_byte + k) = (*(mask + k) & *in_byte) ? white : black; }
    }
  } else {
    out_off = outbuf;
    in_off = inbuf;

    for (y = 0; y < img->ny; y++, out_off += img->nx, in_off += sll) {
      x = 0;
      for (in_byte = in_off; in_byte < in_off + sll; in_byte++, x += 8) {
        out_byte = out_off + x;

        if ((x + 8) <= img->nx) {
          for (k = 0; k < 8; k++) { *(out_byte + k) = (*(mask + k) & *in_byte) ? white : black; }
        } else {
          for (k = 0; (x + k) < img->nx; k++) { *(out_byte + k) = (*(mask + k) & *in_byte) ? white : black; }
        }
      }
    }
  }

  img->pixels = outbuf;
  delete[] inbuf;
  img->bps = 8;
}
//============================================================================

unsigned char *SipiIOTiff::cvrt8BitTo1bit(const SipiImage &img, unsigned int &sll)
{
  static unsigned char mask[8] = { 128, 64, 32, 16, 8, 4, 2, 1 };

  unsigned int x, y;

  if ((img.photo != PhotometricInterpretation::MINISWHITE) && (img.photo != PhotometricInterpretation::MINISBLACK)) {
    throw Sipi::SipiImageError("Photometric interpretation is not MINISWHITE or  MINISBLACK");
  }

  if (img.bps != 8) {
    std::string msg = "Bits per sample is not 8 but: " + std::to_string(img.bps);
    throw Sipi::SipiImageError(msg);
  }

  sll = (img.nx + 7) / 8;
  unsigned char *outbuf = new unsigned char[sll * img.ny];

  if (img.photo == PhotometricInterpretation::MINISBLACK) {
    memset(outbuf, 0L, sll * img.ny);
    for (y = 0; y < img.ny; y++) {
      for (x = 0; x < img.nx; x++) {
        outbuf[y * sll + (x / 8)] |= (img.pixels[y * img.nx + x] > 128) ? mask[x % 8] : !mask[x % 8];
      }
    }
  } else {// must be MINISWHITE
    memset(outbuf, -1L, sll * img.ny);
    for (y = 0; y < img.ny; y++) {
      for (x = 0; x < img.nx; x++) {
        outbuf[y * sll + (x / 8)] |= (img.pixels[y * img.nx + x] > 128) ? !mask[x % 8] : mask[x % 8];
      }
    }
  }
  return outbuf;
}
//============================================================================

}// namespace Sipi
