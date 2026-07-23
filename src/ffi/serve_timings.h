/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * Per-phase timing capture for one `sipi_serve_image` call (strangler-fig; ADR-0013).
 *
 * The engine emits no OpenTelemetry spans of its own — only the Rust shell runs
 * an OTel tracer + exporter. To break the opaque `sipi.serve` span into
 * read/decode/rotate/quality/watermark/encode phases in the Tempo waterfall
 * WITHOUT an in-engine SDK, the engine merely *times* each phase into a
 * thread-local accumulator; the shell reads it back over the seam
 * (`sipi_serve_timings_take`) and mints one child span per phase with explicit
 * start/end timestamps.
 *
 * Thread-local because the seam is synchronous and single-threaded per call: the
 * shell resets the accumulator implicitly at each `sipi_serve_image` entry, the
 * phase timers run on that same worker thread (decode/convert in
 * `build_image_response`, encode in the streamed producer's `produce`, all
 * before the call returns), and the shell reads it on the same thread right
 * after. The pattern mirrors the trace-context thread-locals in `logging/logger`.
 */
#ifndef SIPI_FFI_SERVE_TIMINGS_H
#define SIPI_FFI_SERVE_TIMINGS_H

#include <chrono>

#include "ffi/sipi_ffi.h"

namespace Sipi::ffi {

/*! Clear the calling thread's accumulator and stamp the phase-offset origin.
 *  Called at the top of `sipi_serve_image`, before any [`PhaseSpanTimer`]. */
void serve_timings_reset();

/*! Copy the calling thread's accumulator into `*out` (no-op on NULL). Backs the
 *  `sipi_serve_timings_take` seam entry. */
void serve_timings_export(SipiServeTimings *out);

/*! RAII timer for one serve phase: records `[construction, destruction)` against
 *  `phase` in the thread-local accumulator, as an offset from the last
 *  [`serve_timings_reset`] plus a duration. If the scope exits via an exception,
 *  the phase is also marked failed (detected in the destructor via
 *  `std::uncaught_exceptions()`). Re-timing a phase overwrites it; an
 *  out-of-range index is ignored. Times only — it mints no OTel span; the shell
 *  does that from the exported timings. */
class PhaseTimer
{
public:
  explicit PhaseTimer(SipiPhase phase);
  ~PhaseTimer();
  PhaseTimer(const PhaseTimer &) = delete;
  PhaseTimer &operator=(const PhaseTimer &) = delete;
  PhaseTimer(PhaseTimer &&) = delete;
  PhaseTimer &operator=(PhaseTimer &&) = delete;

private:
  SipiPhase phase_;
  int uncaught_on_entry_;
  std::chrono::steady_clock::time_point start_;
};

}// namespace Sipi::ffi

#endif// SIPI_FFI_SERVE_TIMINGS_H
