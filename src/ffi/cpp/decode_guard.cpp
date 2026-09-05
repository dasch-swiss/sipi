/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "decode_guard.h"

namespace Sipi::ffi {

FullLaneAcquisition acquire_full_lane_budget(SipiMemoryBudget *memory_budget,
  std::size_t large_decode_threshold_bytes,
  std::size_t estimated_bytes,
  std::function<void()> on_release)
{
  const bool is_full_lane = estimated_bytes >= large_decode_threshold_bytes;
  if (memory_budget == nullptr || !is_full_lane) { return {}; }

  FullLaneAcquisition out;
  out.consulted = true;
  out.result = memory_budget->try_acquire(estimated_bytes);
  out.allowed = out.result.allowed;
  if (out.allowed) { out.guard.emplace(*memory_budget, estimated_bytes, true, std::move(on_release)); }
  return out;
}

}// namespace Sipi::ffi
