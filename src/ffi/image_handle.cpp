/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * The `sipi_image_*` handle family — the engine surface behind the Lua
 * runtime's `SipiImage` userdata, plus the `helper.filename_hash` and
 * file-mimetype helpers. The full contract (ownership, error channel,
 * reentrancy, callback rules, geometry validation) is documented on the
 * declarations in `ffi/sipi_ffi.h`.
 *
 * Error text emitted here is the bare engine message; the Lua runtime
 * prepends the binding-specific prefix ("SipiImage.new(): " …), preserving
 * the historical script-visible shapes from one place.
 */

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "util/Error.h"
#include "util/Parsing.h"

#include "SipiFilenameHash.h"
#include "SipiImage.h"
#include "SipiImageError.h"

#include "ffi/serve_response.h"// sipi_guard, SipiStatus
#include "ffi/sipi_ffi.h"

namespace {

/*! NULL → "" (the seam's input normalization). */
const char *nz(const char *s) { return s != nullptr ? s : ""; }

void emit_str(SipiStrFn emit, void *ctx, const std::string &s)
{
  if (emit != nullptr) { emit(ctx, s.c_str()); }
}

/*! Minimal JSON string escaping for the exif/gps emission. */
std::string json_escape(const std::string &s)
{
  std::string out;
  out.reserve(s.size() + 8);
  for (const char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[8];
        (void)snprintf(buf, sizeof(buf), "\\u%04x", c & 0xFF);
        out += buf;
      } else {
        out += c;
      }
    }
  }
  return out;
}

std::string json_quote(const std::string &s) { return "\"" + json_escape(s) + "\""; }

}// namespace

/*! The handle: the image plus the source path recorded at creation (consumed
 *  by mimetype_consistency and the tostring rendering). */
struct SipiImageHandle
{
  Sipi::SipiImage image;
  std::string filename;
};

extern "C" SipiImageHandle *sipi_image_new(const char *path,
  const char *region,
  const char *size,
  int reduce,
  int has_reduce,
  const char *original,
  SipiStrFn err,
  void *err_ctx)
{
  // Pointer-returning entry: cannot use the int-returning sipi_guard; the
  // hand-rolled catch-all is the same exception wall.
  try {
    const std::string imgpath = nz(path);
    const std::string original_str = nz(original);

    std::shared_ptr<Sipi::SipiRegion> reg;
    std::shared_ptr<Sipi::SipiSize> siz;
    if (region != nullptr && region[0] != '\0') { reg = std::make_shared<Sipi::SipiRegion>(region); }
    if (size != nullptr && size[0] != '\0') { siz = std::make_shared<Sipi::SipiSize>(size); }
    if (has_reduce != 0) {
      if (reduce < 0) {
        emit_str(err, err_ctx, "reduce must be >= 0");
        return nullptr;
      }
      siz = std::make_shared<Sipi::SipiSize>(reduce);
    }

    auto handle = std::make_unique<SipiImageHandle>();
    handle->filename = imgpath;
    if (!original_str.empty()) {
      handle->image.readSource(imgpath, reg, siz, original_str);
    } else {
      handle->image.read(imgpath, reg, siz);
    }
    return handle.release();
  } catch (const Sipi::SipiImageError &e) {
    emit_str(err, err_ctx, e.to_string());
    return nullptr;
  } catch (const Sipi::SipiError &e) {
    std::ostringstream ss;
    ss << e;
    emit_str(err, err_ctx, ss.str());
    return nullptr;
  } catch (const std::exception &e) {
    emit_str(err, err_ctx, e.what());
    return nullptr;
  } catch (...) {
    emit_str(err, err_ctx, "unknown engine error");
    return nullptr;
  }
}

extern "C" void sipi_image_free(SipiImageHandle *img)
{
  delete img;// NULL-safe by C++ delete semantics
}

extern "C" int sipi_image_handle_dims(const SipiImageHandle *img, uint64_t *nx, uint64_t *ny, int *orientation)
{
  return Sipi::ffi::sipi_guard([&] {
    *nx = img->image.getNx();
    *ny = img->image.getNy();
    *orientation = static_cast<int>(img->image.getOrientation());
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

extern "C" int
  sipi_image_file_dims(const char *path, uint64_t *nx, uint64_t *ny, int *orientation, SipiStrFn err, void *err_ctx)
{
  return Sipi::ffi::sipi_guard([&] {
    Sipi::SipiImage img;
    Sipi::SipiImgInfo info;
    try {
      info = img.read_shape(nz(path));
    } catch (const Sipi::InfoError &) {
      emit_str(err, err_ctx, "Couldn't get dimensions");
      return 1;
    } catch (const Sipi::SipiImageError &e) {
      emit_str(err, err_ctx, e.to_string());
      return 1;
    }
    *nx = info.width;
    *ny = info.height;
    *orientation = static_cast<int>(info.orientation);
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

extern "C" int sipi_image_crop(SipiImageHandle *img, const char *iiif_region, SipiStrFn err, void *err_ctx)
{
  return Sipi::ffi::sipi_guard([&] {
    std::shared_ptr<Sipi::SipiRegion> reg;
    try {
      reg = std::make_shared<Sipi::SipiRegion>(nz(iiif_region));
    } catch (const Sipi::SipiError &e) {
      std::ostringstream ss;
      ss << e;
      emit_str(err, err_ctx, ss.str());
      return 1;
    }
    img->image.crop(reg);
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

extern "C" int sipi_image_scale(SipiImageHandle *img, const char *iiif_size, SipiStrFn err, void *err_ctx)
{
  return Sipi::ffi::sipi_guard([&] {
    size_t nx = 0;
    size_t ny = 0;
    try {
      Sipi::SipiSize size(nz(iiif_size));
      int r = 0;
      bool ro = false;
      size.get_size(img->image.getNx(), img->image.getNy(), nx, ny, r, ro);
    } catch (const Sipi::SipiError &e) {
      std::ostringstream ss;
      ss << e;
      emit_str(err, err_ctx, ss.str());
      return 1;
    }
    img->image.scale(nx, ny);
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

extern "C" int sipi_image_rotate(SipiImageHandle *img, float angle, int mirror, SipiStrFn err, void *err_ctx)
{
  (void)err;
  (void)err_ctx;
  return Sipi::ffi::sipi_guard([&] {
    img->image.rotate(angle, mirror != 0);
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

extern "C" int sipi_image_topleft(SipiImageHandle *img)
{
  return Sipi::ffi::sipi_guard([&] {
    img->image.set_topleft();
    img->image.setOrientation(Sipi::TOPLEFT);
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

extern "C" int sipi_image_watermark(SipiImageHandle *img, const char *wmfile, SipiStrFn err, void *err_ctx)
{
  return Sipi::ffi::sipi_guard([&] {
    try {
      img->image.add_watermark(nz(wmfile));
    } catch (const Sipi::SipiImageError &e) {
      emit_str(err, err_ctx, e.to_string());
      return 1;
    }
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

namespace {

enum class ExifKind {
  Ascii,
  Byte,
  Short,
  UShort,
  Long,
  ULong,
  Float,
  Rational,
  URational,
  URationalV,
};

/*! Tag → (exif group, value kind); the script-visible EXIF allowlist. */
const std::unordered_map<std::string, std::pair<std::string, ExifKind>> &exif_taglist()
{
  static const std::unordered_map<std::string, std::pair<std::string, ExifKind>> taglist{
    { "DocumentName", { "Image", ExifKind::Ascii } },
    { "ImageDescription", { "Image", ExifKind::Ascii } },
    { "Make", { "Image", ExifKind::Ascii } },
    { "Model", { "Image", ExifKind::Ascii } },
    { "Orientation", { "Image", ExifKind::UShort } },
    { "XResolution", { "Image", ExifKind::URational } },
    { "YResolution", { "Image", ExifKind::URational } },
    { "PageName", { "Image", ExifKind::Ascii } },
    { "XPosition", { "Image", ExifKind::URational } },
    { "YPosition", { "Image", ExifKind::URational } },
    { "ResolutionUnit", { "Image", ExifKind::UShort } },
    { "PageNumber", { "Image", ExifKind::UShort } },
    { "Software", { "Image", ExifKind::Ascii } },
    { "ModifyDate", { "Image", ExifKind::Ascii } },
    { "DateTime", { "Image", ExifKind::Ascii } },
    { "Artist", { "Image", ExifKind::Ascii } },
    { "HostComputer", { "Image", ExifKind::Ascii } },
    { "TileWidth", { "Image", ExifKind::ULong } },
    { "TileLength", { "Image", ExifKind::ULong } },
    { "ImageID", { "Image", ExifKind::Ascii } },
    { "BatteryLevel", { "Image", ExifKind::URational } },
    { "Copyright", { "Image", ExifKind::Ascii } },
    { "ImageNumber", { "Image", ExifKind::ULong } },
    { "ImageHistory", { "Image", ExifKind::Ascii } },
    { "UniqueCameraModel", { "Image", ExifKind::Ascii } },
    { "CameraSerialNumber", { "Image", ExifKind::Ascii } },
    { "CameraLabel", { "Image", ExifKind::Ascii } },
    { "ExposureTime", { "Photo", ExifKind::URational } },
    { "FNumber", { "Photo", ExifKind::URational } },
    { "ExposureProgram", { "Photo", ExifKind::UShort } },
    { "SpectralSensitivity", { "Photo", ExifKind::Ascii } },
    { "ISOSpeedRatings", { "Photo", ExifKind::UShort } },
    { "SensitivityType", { "Photo", ExifKind::UShort } },
    { "StandardOutputSensitivity", { "Photo", ExifKind::ULong } },
    { "RecommendedExposureIndex", { "Photo", ExifKind::ULong } },
    { "ISOSpeed", { "Photo", ExifKind::ULong } },
    { "ISOSpeedLatitudeyyy", { "Photo", ExifKind::ULong } },
    { "ISOSpeedLatitudezzz", { "Photo", ExifKind::ULong } },
    { "DateTimeOriginal", { "Photo", ExifKind::Ascii } },
    { "DateTimeDigitized", { "Photo", ExifKind::Ascii } },
    { "OffsetTime", { "Photo", ExifKind::Ascii } },
    { "OffsetTimeOriginal", { "Photo", ExifKind::Ascii } },
    { "OffsetTimeDigitized", { "Photo", ExifKind::Ascii } },
    { "ShutterSpeedValue", { "Photo", ExifKind::Rational } },
    { "ApertureValue", { "Photo", ExifKind::URational } },
    { "BrightnessValue", { "Photo", ExifKind::Rational } },
    { "ExposureBiasValue", { "Photo", ExifKind::Rational } },
    { "MaxApertureValue", { "Photo", ExifKind::URational } },
    { "SubjectDistance", { "Photo", ExifKind::URational } },
    { "MeteringMode", { "Photo", ExifKind::UShort } },
    { "LightSource", { "Photo", ExifKind::UShort } },
    { "Flash", { "Photo", ExifKind::UShort } },
    { "FocalLength", { "Photo", ExifKind::URational } },
    { "UserComment", { "Photo", ExifKind::Ascii } },
    { "SubSecTime", { "Photo", ExifKind::Ascii } },
    { "SubSecTimeOriginal", { "Photo", ExifKind::Ascii } },
    { "SubSecTimeDigitized", { "Photo", ExifKind::Ascii } },
    { "Temperature", { "Photo", ExifKind::Rational } },
    { "Humidity", { "Photo", ExifKind::URational } },
    { "Pressure", { "Photo", ExifKind::URational } },
    { "WaterDepth", { "Photo", ExifKind::Rational } },
    { "Acceleration", { "Photo", ExifKind::URational } },
    { "CameraElevationAngle", { "Photo", ExifKind::Rational } },
    { "RelatedSoundFile", { "Photo", ExifKind::Ascii } },
    { "FlashEnergy", { "Photo", ExifKind::URational } },
    { "FocalPlaneXResolution", { "Photo", ExifKind::URational } },
    { "FocalPlaneYResolution", { "Photo", ExifKind::URational } },
    { "FocalPlaneResolutionUnit", { "Photo", ExifKind::UShort } },
    { "SceneCaptureType", { "Photo", ExifKind::UShort } },
    { "GainControl", { "Photo", ExifKind::UShort } },
    { "Contrast", { "Photo", ExifKind::UShort } },
    { "Saturation", { "Photo", ExifKind::UShort } },
    { "Sharpness", { "Photo", ExifKind::UShort } },
    { "SubjectDistanceRange", { "Photo", ExifKind::UShort } },
    { "ImageUniqueID", { "Photo", ExifKind::Ascii } },
    { "OwnerName", { "Photo", ExifKind::Ascii } },
    { "SerialNumber", { "Photo", ExifKind::Ascii } },
    { "LensInfo", { "Photo", ExifKind::URationalV } },
    { "LensMake", { "Photo", ExifKind::Ascii } },
    { "LensModel", { "Photo", ExifKind::Ascii } },
    { "LensSerialNumber", { "Photo", ExifKind::Ascii } },
  };
  return taglist;
}

}// namespace

extern "C" int sipi_image_exif_get(const SipiImageHandle *img, const char *tag, SipiStrFn emit, void *ctx)
{
  return Sipi::ffi::sipi_guard([&] {
    const auto &taglist = exif_taglist();
    const auto tagiter = taglist.find(nz(tag));
    if (tagiter == taglist.end()) { return 1; }

    const std::shared_ptr<Sipi::Exif> exif = img->image.getExif();
    if (exif == nullptr) { return 3; }

    const std::string fulltag = "Exif." + tagiter->second.first + "." + tagiter->first;
    std::ostringstream json;
    switch (tagiter->second.second) {
    case ExifKind::Ascii: {
      std::string v;
      if (!exif->getValByKey(fulltag, v)) { return 2; }
      json << json_quote(v);
      break;
    }
    case ExifKind::Byte: {
      char v{ 0 };
      if (!exif->getValByKey(fulltag, v)) { return 2; }
      json << static_cast<int>(v);
      break;
    }
    case ExifKind::Short: {
      short v{ 0 };
      if (!exif->getValByKey(fulltag, v)) { return 2; }
      json << v;
      break;
    }
    case ExifKind::UShort: {
      unsigned short v{ 0 };
      if (!exif->getValByKey(fulltag, v)) { return 2; }
      json << v;
      break;
    }
    case ExifKind::Long: {
      int v{ 0 };
      if (!exif->getValByKey(fulltag, v)) { return 2; }
      json << v;
      break;
    }
    case ExifKind::ULong: {
      unsigned int v{ 0 };
      if (!exif->getValByKey(fulltag, v)) { return 2; }
      json << v;
      break;
    }
    case ExifKind::Float: {
      float v{ 0 };
      if (!exif->getValByKey(fulltag, v)) { return 2; }
      json << v;
      break;
    }
    // Rational kinds all render as the historical two-element [num, den].
    case ExifKind::Rational:
    case ExifKind::URational:
    case ExifKind::URationalV: {
      Exiv2::Rational v{ 0, 1 };
      if (!exif->getValByKey(fulltag, v)) { return 2; }
      json << "[" << v.first << "," << v.second << "]";
      break;
    }
    }
    emit_str(emit, ctx, json.str());
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

extern "C" int sipi_image_gps(const SipiImageHandle *img, SipiStrFn emit, void *ctx)
{
  return Sipi::ffi::sipi_guard([&] {
    const std::shared_ptr<Sipi::Exif> exif = img->image.getExif();
    if (exif == nullptr) { return 3; }

    const auto get_ref = [&](const char *key) -> std::string {
      char ref{ '\0' };
      if (!exif->getValByKey(std::string(key), ref) || ref == '\0') { return {}; }
      return std::string(1, ref);
    };
    const auto get_rational = [&](const char *key) -> double {
      Exiv2::Rational v{ 0, 1 };
      if (!exif->getValByKey(std::string(key), v) || v.second == 0) { return 0.0; }
      return static_cast<double>(v.first) / static_cast<double>(v.second);
    };
    const auto get_triple = [&](const char *key, double out[3]) {
      std::vector<Exiv2::Rational> v{ { 0, 1 }, { 0, 1 }, { 0, 1 } };
      if (!exif->getValByKey(std::string(key), v)) { v = { { 0, 1 }, { 0, 1 }, { 0, 1 } }; }
      for (int i = 0; i < 3; ++i) {
        out[i] = (i < static_cast<int>(v.size()) && v[i].second != 0)
                   ? static_cast<double>(v[i].first) / static_cast<double>(v[i].second)
                   : 0.0;
      }
    };

    std::ostringstream json;
    json << "{";
    const auto put_ref = [&](const char *name, const char *key) {
      json << json_quote(name) << ":" << json_quote(get_ref(key)) << ",";
    };
    const auto put_num = [&](const char *name, const char *key) {
      json << json_quote(name) << ":" << get_rational(key) << ",";
    };
    const auto put_triple = [&](const char *name, const char *key, bool trailing_comma = true) {
      double t[3];
      get_triple(key, t);
      json << json_quote(name) << ":[" << t[0] << "," << t[1] << "," << t[2] << "]";
      if (trailing_comma) { json << ","; }
    };

    put_ref("GPSLatitudeRef", "Exif.GPSInfo.GPSLatitudeRef");
    put_triple("GPSLatitude", "Exif.GPSInfo.GPSLatitude");
    put_ref("GPSLongitudeRef", "Exif.GPSInfo.GPSLongitudeRef");
    put_triple("GPSLongitude", "Exif.GPSInfo.GPSLongitude");
    put_ref("GPSAltitudeRef", "Exif.GPSInfo.GPSAltitudeRef");
    put_num("GPSAltitude", "Exif.GPSInfo.GPSAltitude");
    put_triple("GPSTimeStamp", "Exif.GPSInfo.GPSTimeStamp");
    put_ref("GPSSpeedRef", "Exif.GPSInfo.GPSSpeedRef");
    put_num("GPSSpeed", "Exif.GPSInfo.GPSSpeed");
    put_ref("GPSTrackRef", "Exif.GPSInfo.GPSTrackRef");
    put_num("GPSTrack", "Exif.GPSInfo.GPSTrack");
    put_ref("GPSImgDirectionRef", "Exif.GPSInfo.GPSImgDirectionRef");
    put_num("GPSImgDirection", "Exif.GPSInfo.GPSImgDirection");
    put_ref("GPSDestLatitudeRef", "Exif.GPSInfo.GPSDestLatitudeRef");
    put_triple("GPSDestLatitude", "Exif.GPSInfo.GPSDestLatitude");
    put_ref("GPSDestLongitudeRef", "Exif.GPSInfo.GPSDestLongitudeRef");
    put_triple("GPSDestLongitude", "Exif.GPSInfo.GPSDestLongitude");
    put_ref("GPSDestBearingRef", "Exif.GPSInfo.GPSDestBearingRef");
    put_num("GPSDestBearing", "Exif.GPSInfo.GPSDestBearing");
    put_ref("GPSDestDistanceRef", "Exif.GPSInfo.GPSDestDistanceRef");
    put_num("GPSDestDistance", "Exif.GPSInfo.GPSDestDistance");
    json << json_quote("GPSHPositioningError") << ":" << get_rational("Exif.GPSInfo.GPSHPositioningError");
    json << "}";
    emit_str(emit, ctx, json.str());
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

extern "C" int sipi_image_mimetype_consistency(const SipiImageHandle *img,
  const char *mimetype,
  const char *filename,
  int *consistent,
  SipiStrFn err,
  void *err_ctx)
{
  return Sipi::ffi::sipi_guard([&] {
    try {
      *consistent = shttps::Parsing::checkMimeTypeConsistency(img->filename, nz(filename), nz(mimetype)) ? 1 : 0;
    } catch (const Sipi::SipiImageError &e) {
      emit_str(err, err_ctx, e.to_string());
      return 1;
    } catch (const shttps::Error &e) {
      std::ostringstream ss;
      ss << e;
      emit_str(err, err_ctx, ss.str());
      return 1;
    }
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

namespace {

/*! Map the Lua-facing compression keys onto the engine's parameter enum.
 *  Values arrive validated by the Lua runtime; an unknown key here is an
 *  internal contract violation, not a script error. */
bool build_comp_params(const char *const *keys,
  const char *const *values,
  size_t n,
  Sipi::SipiCompressionParams &out)
{
  static const std::unordered_map<std::string, Sipi::SipiCompressionParamName> keymap{
    { "Sprofile", Sipi::J2K_Sprofile },
    { "Creversible", Sipi::J2K_Creversible },
    { "Clayers", Sipi::J2K_Clayers },
    { "Clevels", Sipi::J2K_Clevels },
    { "Corder", Sipi::J2K_Corder },
    { "Cprecincts", Sipi::J2K_Cprecincts },
    { "Cblk", Sipi::J2K_Cblk },
    { "Cuse_sop", Sipi::J2K_Cuse_sop },
    { "rates", Sipi::J2K_rates },
    { "quality", Sipi::JPEG_QUALITY },
  };
  for (size_t i = 0; i < n; ++i) {
    const auto it = keymap.find(nz(keys[i]));
    if (it == keymap.end()) { return false; }
    out[it->second] = nz(values[i]);
  }
  return true;
}

/*! Service-File stamping: build + attach the Essentials packet (ADR-0009 /
 *  ADR-0010) and force pyramidal TIFF. */
void stamp_service_file(SipiImageHandle *img,
  const std::string &ftype,
  const std::string &origname,
  const std::string &mimetype,
  Sipi::SipiCompressionParams &comp_params)
{
  Sipi::EssentialsFields fields;
  fields.origname = origname;
  fields.mimetype = mimetype;
  fields.hash_type = shttps::HashType::sha256;
  fields.data_chksum = img->image.compute_pixel_hash(fields.hash_type);
  if (img->image.getIcc() != nullptr) {
    fields.use_icc = true;
    fields.icc_profile = img->image.getIcc()->iccBytes();
  }
  fields.img_w = static_cast<std::uint32_t>(img->image.getNx());
  fields.img_h = static_cast<std::uint32_t>(img->image.getNy());
  fields.nc = static_cast<std::uint32_t>(img->image.getNc());
  fields.bps = static_cast<std::uint32_t>(img->image.getBps());
  img->image.essential_metadata(Sipi::Essentials{ std::move(fields) });
  if (ftype == "tif") { comp_params[Sipi::TIFF_Pyramid] = "yes"; }
}

}// namespace

extern "C" int sipi_image_write(SipiImageHandle *img,
  const char *ftype,
  const char *path,
  const char *const *param_keys,
  const char *const *param_values,
  size_t n_params,
  const char *origname,
  const char *mimetype,
  SipiStrFn err,
  void *err_ctx)
{
  return Sipi::ffi::sipi_guard([&] {
    Sipi::SipiCompressionParams comp_params;
    if (!build_comp_params(param_keys, param_values, n_params, comp_params)) {
      emit_str(err, err_ctx, "invalid compression parameter");
      return 1;
    }
    const std::string ftype_str = nz(ftype);
    const std::string origname_str = nz(origname);
    const std::string mimetype_str = nz(mimetype);
    if (!origname_str.empty() && !mimetype_str.empty()) {
      stamp_service_file(img, ftype_str, origname_str, mimetype_str, comp_params);
    }
    try {
      img->image.write(ftype_str, nz(path), comp_params.empty() ? nullptr : &comp_params);
    } catch (const Sipi::SipiImageError &e) {
      emit_str(err, err_ctx, e.to_string());
      return 1;
    }
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

extern "C" int sipi_image_send(SipiImageHandle *img,
  const char *ftype,
  const char *const *param_keys,
  const char *const *param_values,
  size_t n_params,
  SipiWriteFn write,
  void *write_ctx,
  SipiStrFn err,
  void *err_ctx)
{
  return Sipi::ffi::sipi_guard([&] {
    Sipi::SipiCompressionParams comp_params;
    if (!build_comp_params(param_keys, param_values, n_params, comp_params)) {
      emit_str(err, err_ctx, "invalid compression parameter");
      return 1;
    }
    try {
      img->image.write(
        nz(ftype), Sipi::CallbackSink{ write, write_ctx }, comp_params.empty() ? nullptr : &comp_params);
    } catch (const Sipi::SipiImageError &e) {
      emit_str(err, err_ctx, e.to_string());
      return 1;
    }
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

extern "C" int sipi_image_tostring(const SipiImageHandle *img, SipiStrFn emit, void *ctx)
{
  return Sipi::ffi::sipi_guard([&] {
    std::ostringstream ss;
    ss << "File: " << img->filename;
    ss << img->image;
    emit_str(emit, ctx, ss.str());
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

extern "C" int sipi_filename_hash(const char *filename, SipiStrFn emit, void *ctx)
{
  return Sipi::ffi::sipi_guard([&] {
    try {
      SipiFilenameHash hash(nz(filename));
      emit_str(emit, ctx, hash.filepath());
    } catch (const shttps::Error &e) {
      std::ostringstream ss;
      ss << e;
      emit_str(emit, ctx, ss.str());
      return 1;
    }
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

extern "C" int sipi_file_mimetype(const char *path, SipiKVFn emit, void *ctx, SipiStrFn err, void *err_ctx)
{
  return Sipi::ffi::sipi_guard([&] {
    try {
      const auto mimetype = shttps::Parsing::getFileMimetype(nz(path));
      if (emit != nullptr) {
        emit(ctx, "mimetype", mimetype.first.c_str());
        if (!mimetype.second.empty()) { emit(ctx, "charset", mimetype.second.c_str()); }
      }
    } catch (const shttps::Error &e) {
      std::ostringstream ss;
      ss << e;
      emit_str(err, err_ctx, ss.str());
      return 1;
    }
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

extern "C" int sipi_file_mimeconsistency(const char *path,
  const char *filename,
  const char *expected_mimetype,
  int *consistent,
  SipiStrFn err,
  void *err_ctx)
{
  return Sipi::ffi::sipi_guard([&] {
    try {
      *consistent =
        shttps::Parsing::checkMimeTypeConsistency(nz(path), nz(filename), nz(expected_mimetype)) ? 1 : 0;
    } catch (const shttps::Error &e) {
      std::ostringstream ss;
      ss << e;
      emit_str(err, err_ctx, ss.str());
      return 1;
    }
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}
