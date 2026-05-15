/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "cli/access_file_orchestrator.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "Logger.h"
#include "SipiImage.hpp"
#include "SipiImageError.hpp"
#include "SipiRegion.h"
#include "SipiReport.h"
#include "SipiSize.h"
#include "metadata/essentials.h"
#include "metadata/icc.h"
#include "observability/sentry.h"

namespace Sipi::cli {

namespace {

std::string lower_extension(const std::string &path)
{
  const auto dot = path.rfind('.');
  if (dot == std::string::npos || dot + 1 >= path.size()) { return {}; }
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext;
}

/// Map the output extension or `--format` choice to the codec name accepted
/// by `SipiImage::write` ("jpg" / "jpx" / "tif" / "png"). Returns the format
/// string on success or an empty string + error message on failure.
struct FormatResolution
{
  std::string format;
  std::string error;
};

FormatResolution resolve_output_format(const AccessFileRequest &req)
{
  if (!req.format.empty()) {
    if (req.format == "jpg" || req.format == "jpx" || req.format == "tif" || req.format == "png") {
      return { req.format, {} };
    }
    return { {}, "Unsupported --format value: '" + req.format + "'" };
  }

  const std::string ext = lower_extension(req.output_path);
  if (ext == "jpx" || ext == "jp2") { return { "jpx", {} }; }
  if (ext == "tif" || ext == "tiff") { return { "tif", {} }; }
  if (ext == "jpg" || ext == "jpeg") { return { "jpg", {} }; }
  if (ext == "png") { return { "png", {} }; }
  return { {}, "Not a supported filename extension: '" + ext + "'" };
}

std::shared_ptr<SipiRegion> build_region(const std::vector<int> &region)
{
  if (region.size() < 4) { return nullptr; }
  return std::make_shared<SipiRegion>(region.at(0), region.at(1), region.at(2), region.at(3));
}

struct SizeOrError
{
  std::shared_ptr<SipiSize> size;
  std::string error;
};

SizeOrError build_size(const AccessFileRequest &req)
{
  if (req.reduce > 0) {
    return { std::make_shared<SipiSize>(req.reduce), {} };
  }
  if (!req.size.empty()) {
    try {
      return { std::make_shared<SipiSize>(req.size), {} };
    } catch (const std::exception &e) {
      return { nullptr, std::string{ "Error in size parameter: " } + e.what() };
    }
  }
  if (req.scale > 0) {
    try {
      return { std::make_shared<SipiSize>(req.scale), {} };
    } catch (const std::exception &e) {
      return { nullptr, std::string{ "Error in scale parameter: " } + e.what() };
    }
  }
  return { nullptr, {} };
}

void report_error(const observability::ImageContext &ctx,
  const std::string &phase,
  const std::string &message,
  bool json_output)
{
  observability::capture_image_error(message, phase, ctx);
  log_err("Error %s image: %s", phase.c_str(), message.c_str());
  if (json_output) { emit_json_report(std::cout, ctx, message, phase); }
}

bool apply_orientation_topleft(SipiImage &img)
{
  Orientation orientation = img.getOrientation();
  auto exif = img.getExif();
  if (exif != nullptr) {
    unsigned short ori;
    if (exif->getValByKey("Exif.Image.Orientation", ori)) {
      orientation = static_cast<Orientation>(ori);
    }
  }
  switch (orientation) {
  case TOPLEFT: break;
  case TOPRIGHT: img.rotate(0., true); break;
  case BOTRIGHT: img.rotate(180., false); break;
  case BOTLEFT: img.rotate(180., true); break;
  case LEFTTOP: img.rotate(270., true); break;
  case RIGHTTOP: img.rotate(90., false); break;
  case RIGHTBOT: img.rotate(90., true); break;
  case LEFTBOT: img.rotate(270., false); break;
  default: break;
  }
  if (exif != nullptr) {
    exif->addKeyVal("Exif.Image.Orientation", static_cast<unsigned short>(TOPLEFT));
  }
  img.setOrientation(TOPLEFT);
  return true;
}

void apply_icc(SipiImage &img, const std::string &icc)
{
  if (icc == "sRGB") { img.convertToIcc(Icc(PredefinedProfiles::icc_sRGB), img.getBps()); return; }
  if (icc == "AdobeRGB") { img.convertToIcc(Icc(PredefinedProfiles::icc_AdobeRGB), img.getBps()); return; }
  if (icc == "GRAY") { img.convertToIcc(Icc(PredefinedProfiles::icc_GRAY_D50), img.getBps()); return; }
}

void apply_rotate_mirror(SipiImage &img, float rotate, const std::string &mirror)
{
  if (mirror == "vertical") {
    img.rotate(rotate + 180.0F, true);
  } else if (mirror == "horizontal") {
    img.rotate(rotate, true);
  } else if (rotate != 0.0F) {
    img.rotate(rotate, false);
  }
}

}// namespace

int run_access_file_orchestrator(const AccessFileRequest &req)
{
  set_cli_mode(true);
  if (req.json_output) { set_json_mode(true); }

  const FormatResolution format_res = resolve_output_format(req);
  if (!format_res.error.empty()) {
    log_err("%s", format_res.error.c_str());
    if (req.json_output) { emit_json_cli_arg_error(std::cout, format_res.error); }
    return EXIT_FAILURE;
  }
  const std::string &format = format_res.format;

  std::shared_ptr<SipiRegion> region = build_region(req.region);
  const SizeOrError size_res = build_size(req);
  if (!size_res.error.empty()) {
    log_err("%s", size_res.error.c_str());
    if (req.json_output) { emit_json_cli_arg_error(std::cout, size_res.error); }
    return EXIT_FAILURE;
  }
  const std::shared_ptr<SipiSize> size = size_res.size;

  observability::ImageContext sentry_ctx;
  sentry_ctx.input_file = req.input_path;
  sentry_ctx.output_file = req.output_path;
  sentry_ctx.output_format = format;
  sentry_ctx.file_size_bytes = observability::get_file_size(req.input_path);

  //
  // Read input. The format-handler reader populates the Essentials packet
  // if the source carries one — that's what we gate the Access File
  // contract on (input MUST be a Service File per ADR-0009).
  //
  SipiImage img;
  try {
    img.readSource(req.input_path, region, size);
    if (format == "jpg") {
      img.to8bps();
      img.convertToIcc(Icc(PredefinedProfiles::icc_sRGB), 8);
    }
  } catch (const SipiImageError &err) {
    observability::populate_from_image(sentry_ctx, img);
    report_error(sentry_ctx, "read", err.what(), req.json_output);
    return EXIT_FAILURE;
  } catch (const std::exception &err) {
    observability::populate_from_image(sentry_ctx, img);
    report_error(sentry_ctx, "read", err.what(), req.json_output);
    return EXIT_FAILURE;
  }

  //
  // Validate: input MUST be a Service File (Essentials packet present).
  // Otherwise route the operator at the generic `sipi convert` verb.
  //
  if (!img.essential_metadata().is_set()) {
    const std::string msg = "convert access-file requires a Service File input; " + req.input_path
      + " has no Essentials packet. Use 'sipi convert' for generic format conversion.";
    log_err("%s", msg.c_str());
    if (req.json_output) { emit_json_cli_arg_error(std::cout, msg); }
    return EXIT_FAILURE;
  }

  //
  // Access Files do not carry Essentials. Drop the in-memory packet so
  // the writer has nothing to emit even if the gate were misconfigured.
  //
  img.essential_metadata(Essentials{});

  //
  // Apply transformations.
  //
  try {
    if (req.set_topleft) { apply_orientation_topleft(img); }
    if (!req.icc.empty() && req.icc != "none") { apply_icc(img, req.icc); }
    if (!req.mirror.empty() || req.rotate != 0.0F) {
      apply_rotate_mirror(img, req.rotate, req.mirror);
    }
    if (!req.watermark.empty()) { img.add_watermark(req.watermark); }
  } catch (const SipiImageError &err) {
    observability::populate_from_image(sentry_ctx, img);
    report_error(sentry_ctx, "convert", err.what(), req.json_output);
    return EXIT_FAILURE;
  } catch (const std::exception &err) {
    observability::populate_from_image(sentry_ctx, img);
    report_error(sentry_ctx, "convert", err.what(), req.json_output);
    return EXIT_FAILURE;
  }

  //
  // Write output. No `J2K_FileRole` / `TIFF_FileRole` param is set, so
  // the writer's Essentials-emission gate stays closed (Phase 7/8 +
  // ADR-0010). XMP / IPTC / EXIF / ICC propagate via the standard write
  // paths (ADR-0011).
  //
  SipiCompressionParams params;
  if (req.jpeg_quality > 0) { params[JPEG_QUALITY] = std::to_string(req.jpeg_quality); }

  try {
    img.write(format, req.output_path, &params);
  } catch (const SipiImageError &err) {
    observability::populate_from_image(sentry_ctx, img);
    report_error(sentry_ctx, "write", err.what(), req.json_output);
    return EXIT_FAILURE;
  } catch (const std::exception &err) {
    observability::populate_from_image(sentry_ctx, img);
    report_error(sentry_ctx, "write", err.what(), req.json_output);
    return EXIT_FAILURE;
  }

  if (req.json_output) {
    observability::populate_from_image(sentry_ctx, img);
    emit_json_report(std::cout, sentry_ctx);
  }
  return EXIT_SUCCESS;
}

}// namespace Sipi::cli
