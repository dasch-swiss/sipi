/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "observability/metrics.h"

#include <algorithm>
#include <cctype>

namespace Sipi::observability {

Metrics &Metrics::instance()
{
  static Metrics inst;
  return inst;
}

EssentialsFormat format_from_path(const std::string &path)
{
  const auto dot = path.rfind('.');
  if (dot == std::string::npos || dot + 1 >= path.size()) { return EssentialsFormat::Other; }
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (ext == "jp2" || ext == "jpx") { return EssentialsFormat::Jp2; }
  if (ext == "tif" || ext == "tiff") { return EssentialsFormat::Tiff; }
  if (ext == "jpg" || ext == "jpeg") { return EssentialsFormat::Jpeg; }
  if (ext == "png") { return EssentialsFormat::Png; }
  return EssentialsFormat::Other;
}

Counter &read_shape_fast_path_counter(
  EssentialsFormat format,
  ReadShapeFastPathOutcome outcome)
{
  Metrics &m = Metrics::instance();
  switch (format) {
  case EssentialsFormat::Jp2:
    switch (outcome) {
    case ReadShapeFastPathOutcome::Hit: return m.read_shape_fast_path_jp2_hit;
    case ReadShapeFastPathOutcome::Miss: return m.read_shape_fast_path_jp2_miss;
    case ReadShapeFastPathOutcome::Partial: return m.read_shape_fast_path_jp2_partial;
    case ReadShapeFastPathOutcome::Fallback: return m.read_shape_fast_path_jp2_fallback;
    }
    break;
  case EssentialsFormat::Tiff:
    switch (outcome) {
    case ReadShapeFastPathOutcome::Hit: return m.read_shape_fast_path_tiff_hit;
    case ReadShapeFastPathOutcome::Miss: return m.read_shape_fast_path_tiff_miss;
    case ReadShapeFastPathOutcome::Partial: return m.read_shape_fast_path_tiff_partial;
    case ReadShapeFastPathOutcome::Fallback: return m.read_shape_fast_path_tiff_fallback;
    }
    break;
  case EssentialsFormat::Jpeg:
  case EssentialsFormat::Png:
  case EssentialsFormat::Other:
    // Non-carrier formats don't surface in the fast-path counter
    // (read_shape doesn't run a fast path for them). Route to jp2_miss
    // as a safety valve; in practice this branch is unreachable.
    return m.read_shape_fast_path_jp2_miss;
  }
  return m.read_shape_fast_path_jp2_miss;
}

Counter &essentials_hash_mismatch_counter(EssentialsFormat format)
{
  Metrics &m = Metrics::instance();
  switch (format) {
  case EssentialsFormat::Jp2: return m.essentials_hash_mismatch_jp2;
  case EssentialsFormat::Tiff: return m.essentials_hash_mismatch_tiff;
  case EssentialsFormat::Jpeg: return m.essentials_hash_mismatch_jpeg;
  case EssentialsFormat::Png: return m.essentials_hash_mismatch_png;
  case EssentialsFormat::Other: return m.essentials_hash_mismatch_other;
  }
  return m.essentials_hash_mismatch_other;
}

}// namespace Sipi::observability
