/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/serve_timings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>

// Pin the `SipiServeTimings` layout the Rust shell mirrors by hand
// (`server-rs/src/ffi.rs`), held in lock-step with the Rust `offset_of!`/
// `size_of` test there. The literal `sizeof` (not one derived from
// SIPI_PHASE_COUNT) is deliberate: it fails to compile if the phase count is
// bumped here without the Rust mirror, catching a one-sided drift the offset
// asserts (self-consistent against either side's own count) cannot. The Rust
// test pairs a `sipi_phase_count()` check for the same reason.
static_assert(offsetof(SipiServeTimings, start_ns) == 0, "SipiServeTimings.start_ns offset");
static_assert(offsetof(SipiServeTimings, dur_ns) == SIPI_PHASE_COUNT * sizeof(uint64_t),
  "SipiServeTimings.dur_ns offset");
static_assert(offsetof(SipiServeTimings, present) == 2 * SIPI_PHASE_COUNT * sizeof(uint64_t),
  "SipiServeTimings.present offset");
static_assert(offsetof(SipiServeTimings, failed) == 2 * SIPI_PHASE_COUNT * sizeof(uint64_t) + SIPI_PHASE_COUNT,
  "SipiServeTimings.failed offset");
static_assert(sizeof(SipiServeTimings) == 112, "SipiServeTimings size drifted from src/server-rs/src/ffi.rs");

namespace Sipi::ffi {

namespace {

//! One worker thread's phase accumulator (see `serve_timings.h`). `t0` is the
//! offset origin stamped by `serve_timings_reset`; each phase records its start
//! offset + duration relative to it.
struct Accumulator
{
  std::chrono::steady_clock::time_point t0;
  std::array<std::uint64_t, SIPI_PHASE_COUNT> start_ns{};
  std::array<std::uint64_t, SIPI_PHASE_COUNT> dur_ns{};
  std::array<std::uint8_t, SIPI_PHASE_COUNT> present{};
  std::array<std::uint8_t, SIPI_PHASE_COUNT> failed{};
};

thread_local Accumulator g_accum;

//! Clamp a possibly-negative tick count to a non-negative nanosecond value.
std::uint64_t clamp_ns(std::chrono::steady_clock::duration d)
{
  const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
  return ns < 0 ? 0 : static_cast<std::uint64_t>(ns);
}

}// namespace

void serve_timings_reset()
{
  g_accum.t0 = std::chrono::steady_clock::now();
  g_accum.start_ns.fill(0);
  g_accum.dur_ns.fill(0);
  g_accum.present.fill(0);
  g_accum.failed.fill(0);
}

void serve_timings_export(SipiServeTimings *out)
{
  if (out == nullptr) { return; }
  for (int i = 0; i < SIPI_PHASE_COUNT; ++i) {
    out->start_ns[i] = g_accum.start_ns[static_cast<std::size_t>(i)];
    out->dur_ns[i] = g_accum.dur_ns[static_cast<std::size_t>(i)];
    out->present[i] = g_accum.present[static_cast<std::size_t>(i)];
    out->failed[i] = g_accum.failed[static_cast<std::size_t>(i)];
  }
}

PhaseTimer::PhaseTimer(SipiPhase phase)
  : phase_(phase), uncaught_on_entry_(std::uncaught_exceptions()), start_(std::chrono::steady_clock::now())
{
}

PhaseTimer::~PhaseTimer()
{
  if (phase_ < 0 || phase_ >= SIPI_PHASE_COUNT) { return; }
  const auto end = std::chrono::steady_clock::now();
  const auto idx = static_cast<std::size_t>(phase_);
  g_accum.start_ns[idx] = clamp_ns(start_ - g_accum.t0);
  g_accum.dur_ns[idx] = clamp_ns(end - start_);
  g_accum.present[idx] = 1;
  // More exceptions in flight than at construction → this scope is unwinding
  // because the phase's work threw. Mark it so the shell flags the span errored.
  g_accum.failed[idx] = std::uncaught_exceptions() > uncaught_on_entry_ ? 1 : 0;
}

}// namespace Sipi::ffi
