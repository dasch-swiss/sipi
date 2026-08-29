/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <cassert>

#include <sys/stat.h>

#include "util/Parsing.h"

#include "logging/logger.h"
#include "SipiImage.h"
#include "SipiImageError.h"
#include "error/SipiValueError.h"
#include "observability/metrics.h"
#include "observability/profiling.h"
#include "util/checked_arith.h"
#include "util/Hash.h"

namespace Sipi {

namespace {

// Every pixel-buffer allocation in this file funnels through here: reject
// (throw) the moment `nx * ny * nc * elem` overflows `size_t`, rather than
// letting a wrapped size feed a too-small `std::vector`/`unique_ptr<[]>`
// allocation that a later copy then overruns.
size_t checked_buf_size_or_throw(size_t nx_, size_t ny_, size_t nc_, size_t elem_)
{
  if (const auto sz = checked_buf_size(nx_, ny_, nc_, elem_)) { return *sz; }
  throw SipiImageError("Pixel buffer size overflow (dimensions=" + std::to_string(nx_) + "x" + std::to_string(ny_)
                        + ", channels=" + std::to_string(nc_) + ", elem=" + std::to_string(elem_) + ")");
}

// The single conversion point from a handler's `Result` failure to the
// exception `SipiImage::read` / `read_shape` / `write` promise their callers:
// a `SipiValueError` becomes a `SipiImageError` carrying the same message,
// errno, and source location. `SipiImage::write` additionally checks for
// `ErrorCode::kClientAbort` first, throwing the `SipiImageClientAbortError`
// subtype for that one code and falling through to this helper for every
// other failure.
[[noreturn]] void throw_from_value_error(const SipiValueError &err)
{
  throw SipiImageError(err.raw_message(), err.errnum(), err.location());
}

}// namespace

// SipiImage::io — the static format-handler registry — is defined in
// //src/format_handlers (format_registry.cpp), not here, so the engine does not
// include the concrete SipiIO* handlers and the SipiImage<->handler Bazel
// dependency cycle stays broken. The engine references `io` below; the linker
// resolves its definition from the format_handlers package.

SipiImage::SipiImage()
{
  nx = 0;
  ny = 0;
  nc = 0;
  bps = 0;
  photo = PhotometricInterpretation::INVALID;
  orientation = TOPLEFT;
  xmp = nullptr;
  icc = nullptr;
  iptc = nullptr;
  exif = nullptr;
  skip_metadata = SkipMetadata::SKIP_NONE;
  ensure_exif();
};
//============================================================================

SipiImage::SipiImage(const SipiImage &img_p)
{
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
    bufsiz = checked_buf_size_or_throw(nx, ny, nc, sizeof(unsigned char));
    break;
  }

  case 16: {
    bufsiz = checked_buf_size_or_throw(nx, ny, nc, sizeof(unsigned short));
    break;
  }

  default: {
    bufsiz = 0;
  }
  }

  if (bufsiz > 0 && !img_p.pixels.empty()) { pixels = img_p.pixels; }

  // R11: Null-check metadata before deep copy
  if (img_p.xmp) xmp = std::make_shared<Xmp>(*img_p.xmp);
  if (img_p.icc) icc = std::make_shared<Icc>(*img_p.icc);
  if (img_p.iptc) iptc = std::make_shared<Iptc>(*img_p.iptc);
  if (img_p.exif) exif = std::make_shared<Exif>(*img_p.exif);
  emdata = img_p.emdata;
  skip_metadata = img_p.skip_metadata;
}

//============================================================================

SipiImage::SipiImage(SipiImage &&other) noexcept
  : nx(other.nx), ny(other.ny), nc(other.nc), bps(other.bps), es(std::move(other.es)), orientation(other.orientation),
    photo(other.photo), pixels(std::move(other.pixels)), xmp(std::move(other.xmp)), icc(std::move(other.icc)),
    iptc(std::move(other.iptc)), exif(std::move(other.exif)), emdata(std::move(other.emdata)),
    skip_metadata(other.skip_metadata)
{
  other.pixels.clear();
  other.nx = 0;
  other.ny = 0;
}

//============================================================================

SipiImage::SipiImage(size_t nx_p, size_t ny_p, size_t nc_p, size_t bps_p, PhotometricInterpretation photo_p)
  : nx(nx_p), ny(ny_p), nc(nc_p), bps(bps_p), photo(photo_p)
{
  orientation = TOPLEFT;// assuming default...
  if (((photo == PhotometricInterpretation::MINISWHITE) || (photo == PhotometricInterpretation::MINISBLACK))
      && !((nc == 1) || (nc == 2))) {
    throw SipiImageError("Mismatch in Photometric interpretation and number of channels");
  }

  if ((photo == PhotometricInterpretation::RGB) && !((nc == 3) || (nc == 4))) {
    throw SipiImageError("Mismatch: photometric interpretation is RGB but image has " + std::to_string(nc) + " channels (expected 3 or 4)");
  }

  if ((bps != 8) && (bps != 16)) { throw SipiImageError("Unsupported bits/sample (" + std::to_string(bps) + "), only 8 and 16 are supported"); }

  size_t bufsiz;

  switch (bps) {
  case 8: {
    bufsiz = checked_buf_size_or_throw(nx, ny, nc, sizeof(unsigned char));
    break;
  }

  case 16: {
    bufsiz = checked_buf_size_or_throw(nx, ny, nc, sizeof(unsigned short));
    break;
  }

  default: {
    bufsiz = 0;
  }
  }

  if (bufsiz > 0) {
    pixels.resize(bufsiz);
  } else {
    throw SipiImageError("Image has no pixel content (dimensions: " + std::to_string(nx) + "x" + std::to_string(ny) + ", bps: " + std::to_string(bps) + " — file may be corrupt or empty)");
  }

  xmp = nullptr;
  icc = nullptr;
  iptc = nullptr;
  ensure_exif();
  skip_metadata = SkipMetadata::SKIP_NONE;
}

//============================================================================

SipiImage &SipiImage::operator=(const SipiImage &img_p)
{
  if (this != &img_p) {
    pixels.clear();

    nx = img_p.nx;
    ny = img_p.ny;
    nc = img_p.nc;
    bps = img_p.bps;
    orientation = img_p.orientation;
    es = img_p.es;
    photo = img_p.photo;      // BUG FIX: missing in original operator=
    emdata = img_p.emdata;    // BUG FIX: missing in original operator=
    skip_metadata = img_p.skip_metadata;

    size_t bufsiz;

    switch (bps) {
    case 8: {
      bufsiz = checked_buf_size_or_throw(nx, ny, nc, sizeof(unsigned char));
      break;
    }

    case 16: {
      bufsiz = checked_buf_size_or_throw(nx, ny, nc, sizeof(unsigned short));
      break;
    }

    default: {
      bufsiz = 0;
    }
    }

    if (bufsiz > 0 && !img_p.pixels.empty()) { pixels = img_p.pixels; }

    // R11: Null-check metadata before deep copy, reset if source is null
    if (img_p.xmp)  xmp  = std::make_shared<Xmp>(*img_p.xmp);   else xmp.reset();
    if (img_p.icc)  icc  = std::make_shared<Icc>(*img_p.icc);   else icc.reset();
    if (img_p.iptc) iptc = std::make_shared<Iptc>(*img_p.iptc); else iptc.reset();
    if (img_p.exif) exif = std::make_shared<Exif>(*img_p.exif); else exif.reset();
  }

  return *this;
}

//============================================================================

SipiImage &SipiImage::operator=(SipiImage &&other) noexcept
{
  if (this != &other) {
    nx = other.nx;
    ny = other.ny;
    nc = other.nc;
    bps = other.bps;
    es = std::move(other.es);
    orientation = other.orientation;
    photo = other.photo;
    pixels = std::move(other.pixels);
    xmp = std::move(other.xmp);
    icc = std::move(other.icc);
    iptc = std::move(other.iptc);
    exif = std::move(other.exif);
    emdata = std::move(other.emdata);
    skip_metadata = other.skip_metadata;

    other.pixels.clear();
    other.nx = 0;
    other.ny = 0;
  }
  return *this;
}

//============================================================================

/*!
 * If this image has no Exif, creates an empty one.
 */
void SipiImage::ensure_exif()
{
  if (exif == nullptr) exif = std::make_shared<Exif>();
}
//============================================================================

void SipiImage::set_pixels(std::vector<byte> &&buf, size_t nx_p, size_t ny_p, size_t nc_p, size_t bps_p)
{
  const size_t expected = checked_buf_size_or_throw(nx_p, ny_p, nc_p, bps_p / 8);
  if (buf.size() != expected) {
    throw SipiImageError("Pixel buffer size mismatch: buffer has " + std::to_string(buf.size())
                          + " bytes, geometry expects " + std::to_string(expected) + " bytes (dimensions="
                          + std::to_string(nx_p) + "x" + std::to_string(ny_p) + ", channels=" + std::to_string(nc_p)
                          + ", bps=" + std::to_string(bps_p) + ")");
  }
  pixels = std::move(buf);
  nx = nx_p;
  ny = ny_p;
  nc = nc_p;
  bps = bps_p;
}
//============================================================================


/*!
 * Reads the image from a file by calling the appropriate reader, selected by
 * the file's extension and falling back to every registered handler in turn
 * on a mismatch. Throws `SipiImageError` if no handler recognises the file,
 * or immediately propagates a handler's decode failure as `SipiImageError`
 * without considering the fallback handlers.
 */
void SipiImage::read(const std::string &filepath,
  const std::shared_ptr<SipiRegion> &region,
  const std::shared_ptr<SipiSize> &size,
  bool force_bps_8,
  ScalingQuality scaling_quality)
{
  SIPI_ZONE_N("SipiImage::read");
  size_t pos = filepath.find_last_of('.');
  std::string fext = filepath.substr(pos + 1);
  std::string _fext;

  bool got_file = false;
  _fext.resize(fext.size());
  std::transform(fext.begin(), fext.end(), _fext.begin(), ::tolower);

  auto dispatch_read = [&](const std::string &key) {
    auto result = io[key]->read(this, filepath, region, size, force_bps_8, scaling_quality);
    if (!result) { throw_from_value_error(result.error()); }
    return *result;
  };

  if ((_fext == "tif") || (_fext == "tiff")) {
    got_file = dispatch_read("tif");
  } else if ((_fext == "jpg") || (_fext == "jpeg")) {
    got_file = dispatch_read("jpg");
  } else if (_fext == "png") {
    got_file = dispatch_read("png");
  } else if ((_fext == "jp2") || (_fext == "jpx") || (_fext == "j2k")) {
    got_file = dispatch_read("jpx");
  }

  if (!got_file) {
    for (auto const &iterator : io) {
      auto result = iterator.second->read(this, filepath, region, size, force_bps_8, scaling_quality);
      if (!result) { throw_from_value_error(result.error()); }
      if ((got_file = *result)) break;
    }
  }

  if (!got_file) { throw SipiImageError("Error reading file " + filepath); }
}

//============================================================================

void SipiImage::readSource(const std::string &filepath,
  const std::shared_ptr<SipiRegion> &region,
  const std::shared_ptr<SipiSize> &size)
{
  read(filepath, region, size, false);

  // Corruption tripwire (ADR-0010): if the source carries an Essentials packet,
  // recompute the pixel checksum and compare. On mismatch, log ERROR and continue —
  // serving operates from Service Files, and an operator wants the signal, not an
  // abort. Active deliberate validation lives in `sipi verify service-file`.
  if (emdata.is_set()) {
    shttps::Hash internal_hash(emdata.fields().hash_type);
    internal_hash.add_data(pixels.data(), checked_buf_size_or_throw(nx, ny, nc, bps / 8));
    std::string checksum = internal_hash.hash();
    if (checksum != Essentials::to_hex(emdata.fields().data_chksum)) {
      log_err("Essentials data_chksum mismatch in %s; possible corruption", filepath.c_str());
      Sipi::observability::essentials_hash_mismatch_counter(
        Sipi::observability::format_from_path(filepath)).Increment();
    }
  }
}

//============================================================================


void SipiImage::readSource(const std::string &filepath,
  const std::shared_ptr<SipiRegion> &region,
  const std::shared_ptr<SipiSize> &size,
  const std::string &origname)
{
  (void)origname;  // consumed by the `convert service-file` command (DEV-6540), not here
  readSource(filepath, region, size);
}

//============================================================================

std::vector<std::byte> SipiImage::compute_pixel_hash(shttps::HashType type) const
{
  shttps::Hash digest(type);
  if (!pixels.empty()) { digest.add_data(pixels.data(), checked_buf_size_or_throw(nx, ny, nc, bps / 8)); }
  return Essentials::from_hex(digest.hash());
}

//============================================================================

SipiImgInfo SipiImage::read_shape(const std::string &filepath) const
{
  SIPI_ZONE_N("SipiImage::read_shape");
  size_t pos = filepath.find_last_of('.');
  std::string fext = filepath.substr(pos + 1);
  std::string _fext;

  _fext.resize(fext.size());
  std::transform(fext.begin(), fext.end(), _fext.begin(), ::tolower);

  SipiImgInfo info;
  std::string mimetype = shttps::Parsing::getFileMimetype(filepath).first;
  info.internalmimetype = mimetype;

  auto dispatch_read_shape = [&](const std::string &key) {
    auto result = io[key]->read_shape(filepath);
    if (!result) { throw_from_value_error(result.error()); }
    return *result;
  };

  if ((mimetype == "image/tiff") || (mimetype == "image/x-tiff")) {
    info = dispatch_read_shape("tif");
  } else if ((mimetype == "image/jpeg") || (mimetype == "image/pjpeg")) {
    info = dispatch_read_shape("jpg");
  } else if (mimetype == "image/png") {
    info = dispatch_read_shape("png");
  } else if ((mimetype == "image/jp2") || (mimetype == "image/jpx")) {
    info = dispatch_read_shape("jpx");
  } else {
    throw SipiImageError("unknown mimetype: \"" + mimetype + "\"!");
  }

  if (info.success == SipiImgInfo::FAILURE) {
    for (auto const &iterator : io) {
      auto result = iterator.second->read_shape(filepath);
      if (!result) { throw_from_value_error(result.error()); }
      info = *result;
      if (info.success != SipiImgInfo::FAILURE) break;
    }
  }

  if (info.success == SipiImgInfo::FAILURE) { throw SipiImageError("Could not read file " + filepath); }
  return info;
}

//============================================================================


void SipiImage::getDim(size_t &width, size_t &height) const
{
  width = getNx();
  height = getNy();
}

//============================================================================

void SipiImage::write(const std::string &ftype, const OutputSink &sink, const SipiCompressionParams *params)
{
  // .at(): an unknown ftype must throw, not operator[]-insert a null
  // handler and segfault on the virtual call. Callers pass validated
  // format strings; the historical write() docstring advertised "j2k"
  // (the map key is "jpx"), which is exactly how this fired.
  auto result = io.at(ftype)->write(this, sink, params);
  if (!result) {
    const auto &err = result.error();
    // The HTTP seam dispatches on the exception type to skip Sentry capture
    // for a client-initiated disconnect; every other write failure is a
    // genuine server-side error.
    if (err.code() == ErrorCode::kClientAbort) {
      throw SipiImageClientAbortError(err.raw_message(), err.errnum(), err.location());
    }
    throw_from_value_error(err);
  }
}

void SipiImage::write(const std::string &ftype, const std::string &filepath, const SipiCompressionParams *params)
{
  write(ftype, OutputSink{ FilePath{ filepath } }, params);
}

//============================================================================

std::ostream &operator<<(std::ostream &outstr, const SipiImage &rhs)
{
  outstr << '\n' << "SipiImage with the following parameters:" << '\n';
  outstr << "nx    = " << std::to_string(rhs.nx) << '\n';
  outstr << "ny    = " << std::to_string(rhs.ny) << '\n';
  outstr << "nc    = " << std::to_string(rhs.nc) << '\n';
  outstr << "es    = " << std::to_string(rhs.es.size()) << '\n';
  outstr << "bps   = " << std::to_string(rhs.bps) << '\n';
  outstr << "photo = " << to_string(rhs.photo) << '\n';

  if (rhs.xmp) { outstr << "XMP-Metadata: " << '\n' << *(rhs.xmp) << '\n'; }

  if (rhs.iptc) { outstr << "IPTC-Metadata: " << '\n' << *(rhs.iptc) << '\n'; }

  if (rhs.exif) { outstr << "EXIF-Metadata: " << '\n' << *(rhs.exif) << '\n'; }

  if (rhs.icc) { outstr << "ICC-Metadata: " << '\n' << *(rhs.icc) << '\n'; }

  return outstr;
}

//============================================================================

}// namespace Sipi
