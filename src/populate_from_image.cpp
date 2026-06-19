/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Out-of-line implementation of `Sipi::observability::populate_from_image`.
// Lives under //src:sipi_lib (not //src/observability) because it
// dereferences `Sipi::SipiImage`, currently owned by sipi_lib. Co-locating
// in observability/ would force a //src/observability → //src:sipi_lib edge,
// closing a cycle (sipi_lib already depends on observability for the rest of
// the surface — Metrics, ConnectionMetricsAdapter, capture_image_error, …).
// Move back into observability/sentry.cpp once SipiImage moves to its own
// package per DEV-6388 / DEV-6395.

#include "observability/sentry.h"

#include "SipiImage.h"
#include "metadata/icc.h"

namespace Sipi::observability {

namespace {

std::string predefined_profile_to_string(PredefinedProfiles p)
{
  switch (p) {
  case icc_undefined: return "undefined";
  case icc_unknown: return "unknown/embedded";
  case icc_sRGB: return "sRGB";
  case icc_AdobeRGB: return "AdobeRGB";
  case icc_RGB: return "RGB (custom)";
  case icc_CMYK_standard: return "CMYK (USWebCoatedSWOP)";
  case icc_GRAY_D50: return "Gray D50";
  case icc_LUM_D65: return "Luminance D65";
  case icc_ROMM_GRAY: return "ROMM Gray";
  case icc_LAB: return "L*a*b*";
  default: return "unknown";
  }
}

std::string orientation_to_string(Orientation o)
{
  switch (o) {
  case TOPLEFT: return "TOPLEFT";
  case TOPRIGHT: return "TOPRIGHT";
  case BOTRIGHT: return "BOTRIGHT";
  case BOTLEFT: return "BOTLEFT";
  case LEFTTOP: return "LEFTTOP";
  case RIGHTTOP: return "RIGHTTOP";
  case RIGHTBOT: return "RIGHTBOT";
  case LEFTBOT: return "LEFTBOT";
  default: return "unknown";
  }
}

}// namespace

void populate_from_image(ImageContext &ctx, const SipiImage &img)
{
  ctx.width = img.getNx();
  ctx.height = img.getNy();
  ctx.channels = img.getNc();
  ctx.bps = img.getBps();
  ctx.colorspace = to_string(img.getPhoto());
  ctx.orientation = orientation_to_string(img.getOrientation());

  auto icc = img.getIcc();
  if (icc) {
    ctx.icc_profile_type = predefined_profile_to_string(icc->getProfileType());
  }
}

}// namespace Sipi::observability
