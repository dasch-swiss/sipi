/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "cli/service_file_orchestrator.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <string>
#include <utility>

#include "Logger.h"
#include "SipiImage.hpp"
#include "metadata/essentials.h"
#include "metadata/icc.h"
#include "shttps/Hash.h"
#include "shttps/Parsing.h"

namespace Sipi::cli {

namespace {

/// Lowercased file extension (without the leading dot), or empty if the
/// path has no extension.
std::string lower_extension(const std::string &path)
{
  const auto dot = path.rfind('.');
  if (dot == std::string::npos || dot + 1 >= path.size()) { return {}; }
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext;
}

/// Final path component (everything after the last `/`), or the whole
/// string if no slash is present. Stored verbatim in
/// `EssentialsFields::origname` as the source filename for this run —
/// the tool makes no claim that it is "the original".
std::string basename(const std::string &path)
{
  const auto slash = path.find_last_of('/');
  return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

/// Service File output format: JP2 or pyramidal TIFF only. The CLI
/// surface enforces "no --format" on `convert service-file`, so the
/// orchestrator infers from the output extension alone.
enum class ServiceFormat { Jp2, PyramidalTiff };

[[nodiscard]] std::expected<ServiceFormat, std::string>
detect_output_format(const std::string &output_path)
{
  const std::string ext = lower_extension(output_path);
  if (ext == "jp2" || ext == "jpx") { return ServiceFormat::Jp2; }
  if (ext == "tif" || ext == "tiff") { return ServiceFormat::PyramidalTiff; }
  return std::unexpected(std::string{
    "convert service-file requires a JP2 (.jp2/.jpx) or pyramidal TIFF (.tif/.tiff) "
    "output; got extension '" + ext + "'. Service Files only live in those two "
    "carriers per ADR-0009." });
}

}// namespace

int run_service_file_orchestrator(const ServiceFileRequest &req)
{
  set_cli_mode(true);

  const auto format_or_err = detect_output_format(req.output_path);
  if (!format_or_err) {
    log_err("%s", format_or_err.error().c_str());
    return EXIT_FAILURE;
  }
  const ServiceFormat format = *format_or_err;

  Sipi::SipiImage img;
  try {
    img.readSource(req.input_path, /*region=*/nullptr, /*size=*/nullptr);
  } catch (const std::exception &err) {
    log_err("convert service-file: error reading source: %s", err.what());
    return EXIT_FAILURE;
  }

  // Drop any Essentials packet the source happened to carry — re-encoding
  // through `convert service-file` always produces a fresh packet keyed
  // to the new source path / hash (maintainer decision 2026-05-14).
  img.essential_metadata(Sipi::Essentials{});

  // Orientation normalization (D5 matrix: `convert service-file` accepts
  // only --topleft). Other transforms are intentionally not exposed on
  // this verb — Service Files are baseline reads, not re-encodes.
  if (req.set_topleft) {
    try {
      Sipi::Orientation orientation = img.getOrientation();
      auto exif = img.getExif();
      if (exif != nullptr) {
        unsigned short ori;
        if (exif->getValByKey("Exif.Image.Orientation", ori)) {
          orientation = static_cast<Sipi::Orientation>(ori);
        }
      }
      switch (orientation) {
      case Sipi::TOPLEFT: break;
      case Sipi::TOPRIGHT: img.rotate(0., true); break;
      case Sipi::BOTRIGHT: img.rotate(180., false); break;
      case Sipi::BOTLEFT: img.rotate(180., true); break;
      case Sipi::LEFTTOP: img.rotate(270., true); break;
      case Sipi::RIGHTTOP: img.rotate(90., false); break;
      case Sipi::RIGHTBOT: img.rotate(90., true); break;
      case Sipi::LEFTBOT: img.rotate(270., false); break;
      default: break;
      }
      if (exif != nullptr) {
        exif->addKeyVal("Exif.Image.Orientation", static_cast<unsigned short>(Sipi::TOPLEFT));
      }
      img.setOrientation(Sipi::TOPLEFT);
    } catch (const std::exception &err) {
      log_err("convert service-file: orientation normalization failed: %s", err.what());
      return EXIT_FAILURE;
    }
  }

  // Build the Essentials packet from the post-transformation state.
  Sipi::EssentialsFields fields;
  fields.origname = basename(req.input_path);
  fields.mimetype = shttps::Parsing::getFileMimetype(req.input_path).first;
  fields.hash_type = shttps::HashType::sha256;
  fields.data_chksum = img.compute_pixel_hash(fields.hash_type);

  if (auto icc = img.getIcc(); icc != nullptr) {
    fields.use_icc = true;
    fields.icc_profile = icc->iccBytes();
  }

  fields.img_w = static_cast<std::uint32_t>(img.getNx());
  fields.img_h = static_cast<std::uint32_t>(img.getNy());
  fields.nc = static_cast<std::uint32_t>(img.getNc());
  fields.bps = static_cast<std::uint32_t>(img.getBps());
  // tile_w / tile_h / clevels are codec-emitted; populated by the format
  // handler when the final layout is known. numpages defaults to 0 (single-page).

  img.essential_metadata(Sipi::Essentials{ std::move(fields) });

  // Hand off to the format-handler writer with file_role = "service-file"
  // so the Essentials carrier emission gates open (JP2 UUID box / TIFF tag
  // 65112). Pyramidal TIFF mode is forced for .tif outputs.
  Sipi::SipiCompressionParams params;
  switch (format) {
  case ServiceFormat::Jp2:
    params[Sipi::J2K_FileRole] = "service-file";
    break;
  case ServiceFormat::PyramidalTiff:
    params[Sipi::TIFF_FileRole] = "service-file";
    params[Sipi::TIFF_Pyramid] = "yes";
    break;
  }

  try {
    const std::string format_str = (format == ServiceFormat::Jp2) ? "jpx" : "tif";
    img.write(format_str, req.output_path, &params);
  } catch (const std::exception &err) {
    log_err("convert service-file: error writing %s: %s", req.output_path.c_str(), err.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

}// namespace Sipi::cli
