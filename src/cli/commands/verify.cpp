/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "cli/commands/verify.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <string>

#include "logging/logger.h"
#include "SipiImage.h"
#include "SipiImageError.h"
#include "metadata/essentials.h"
#include "observability/metrics.h"

namespace Sipi::cli {

namespace {

const char *mode_label(VerifyMode mode)
{
  switch (mode) {
  case VerifyMode::Generic: return "verify";
  case VerifyMode::AccessFile: return "verify access-file";
  case VerifyMode::ServiceFile: return "verify service-file";
  }
  return "verify";
}

std::string lower_extension(const std::string &path)
{
  const auto dot = path.rfind('.');
  if (dot == std::string::npos || dot + 1 >= path.size()) { return {}; }
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext;
}

/// Service Files only live in JP2 or pyramidal TIFF carriers per ADR-0009.
/// At verify time we cannot cheaply assert "pyramidal" without parsing
/// the TIFF IFD structure, so the contract narrows to "extension is .tif/.tiff
/// or .jp2/.jpx, and an Essentials packet was successfully parsed."
/// A non-pyramidal TIFF carrying an Essentials packet is already a
/// misconfiguration we surface via the missing-packet path (the
/// writer-side gate from Phase 7 / 8 prevents it on output, and the
/// reader path tolerates it).
bool is_service_file_extension(const std::string &path)
{
  const std::string ext = lower_extension(path);
  return ext == "jp2" || ext == "jpx" || ext == "tif" || ext == "tiff";
}

/// Access Files live in JPEG / PNG / plain-TIFF / JP2-without-Essentials
/// per ADR-0009. We can't cheaply distinguish "plain" vs "pyramidal" TIFF
/// at the CLI surface, so the contract narrows to the no-Essentials
/// assertion which is the load-bearing invariant.
bool is_access_file_extension(const std::string &path)
{
  const std::string ext = lower_extension(path);
  return ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "tif" || ext == "tiff"
    || ext == "jp2" || ext == "jpx";
}

}// namespace

int cmd_verify(const VerifyArgs &args)
{
  set_cli_mode(true);
  if (args.json_output) { set_json_mode(true); }

  const char *label = mode_label(args.mode);

  if (args.mode == VerifyMode::ServiceFile && !is_service_file_extension(args.input_path)) {
    log_err("%s: %s — Service Files live in JP2 or pyramidal TIFF carriers (.jp2/.jpx/.tif/.tiff) per ADR-0009.",
      label,
      args.input_path.c_str());
    return EXIT_FAILURE;
  }
  if (args.mode == VerifyMode::AccessFile && !is_access_file_extension(args.input_path)) {
    log_err("%s: %s — unsupported Access File extension.", label, args.input_path.c_str());
    return EXIT_FAILURE;
  }

  //
  // Decoder-coverage check: read the file via the format-handler reader.
  // A throw here is the decoder telling us the file is unreadable.
  //
  SipiImage img;
  try {
    img.readSource(args.input_path, /*region=*/nullptr, /*size=*/nullptr);
  } catch (const SipiImageError &err) {
    log_err("%s: failed to decode %s: %s", label, args.input_path.c_str(), err.what());
    return EXIT_FAILURE;
  } catch (const std::exception &err) {
    log_err("%s: failed to decode %s: %s", label, args.input_path.c_str(), err.what());
    return EXIT_FAILURE;
  }

  const Essentials &es = img.essential_metadata();

  switch (args.mode) {
  case VerifyMode::Generic:
    // No metadata assertions — read succeeded.
    return EXIT_SUCCESS;

  case VerifyMode::AccessFile:
    if (es.is_set()) {
      log_err("%s: %s carries an Essentials packet — Access Files do not (ADR-0009). "
              "Use `verify service-file` to validate Service Files.",
        label,
        args.input_path.c_str());
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;

  case VerifyMode::ServiceFile: {
    if (!es.is_set()) {
      log_err("%s: %s carries no Essentials packet — Service Files MUST.", label, args.input_path.c_str());
      return EXIT_FAILURE;
    }
    const EssentialsFields &fields = es.fields();

    //
    // Image-shape consistency: the packet must agree with the codec
    // output for the dimensions we already have in hand. Channels /
    // bps are populated on the post-transformation pixel buffer, so
    // we compare those too.
    //
    auto shape_mismatch = [&](const char *which, std::uint32_t expected, std::uint32_t actual) {
      log_err("%s: %s shape field %s mismatch (packet=%u, codec=%u).",
        label,
        args.input_path.c_str(),
        which,
        expected,
        actual);
    };
    bool shape_ok = true;
    if (fields.img_w != static_cast<std::uint32_t>(img.getNx())) {
      shape_mismatch("img_w", fields.img_w, static_cast<std::uint32_t>(img.getNx()));
      shape_ok = false;
    }
    if (fields.img_h != static_cast<std::uint32_t>(img.getNy())) {
      shape_mismatch("img_h", fields.img_h, static_cast<std::uint32_t>(img.getNy()));
      shape_ok = false;
    }
    if (fields.nc != 0 && fields.nc != static_cast<std::uint32_t>(img.getNc())) {
      shape_mismatch("nc", fields.nc, static_cast<std::uint32_t>(img.getNc()));
      shape_ok = false;
    }
    if (fields.bps != 0 && fields.bps != static_cast<std::uint32_t>(img.getBps())) {
      shape_mismatch("bps", fields.bps, static_cast<std::uint32_t>(img.getBps()));
      shape_ok = false;
    }
    if (!shape_ok) { return EXIT_FAILURE; }

    //
    // Pixel-hash check: corruption tripwire (ADR-0010). On mismatch
    // we exit non-zero and log ERROR so the operator gets the signal.
    // (The readSource path emits its own corruption-tripwire log on
    // mismatch too; verify amplifies that to a non-zero exit.)
    //
    std::vector<std::byte> recomputed = img.compute_pixel_hash(fields.hash_type);
    if (recomputed != fields.data_chksum) {
      log_err("%s: %s pixel hash mismatch — possible corruption.", label, args.input_path.c_str());
      Sipi::observability::essentials_hash_mismatch_counter(
        Sipi::observability::format_from_path(args.input_path)).Increment();
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }
  }

  return EXIT_FAILURE;
}

}// namespace Sipi::cli
