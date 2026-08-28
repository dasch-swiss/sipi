/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/serve_timings.h"

#include <gtest/gtest.h>

#include <stdexcept>

using Sipi::ffi::PhaseTimer;
using Sipi::ffi::serve_timings_export;
using Sipi::ffi::serve_timings_reset;

namespace {

SipiServeTimings take()
{
  SipiServeTimings out{};
  serve_timings_export(&out);
  return out;
}

// A reset accumulator exports as all-absent / all-zero.
TEST(ServeTimings, ResetClearsEverything)
{
  {
    PhaseTimer t(SIPI_PHASE_DECODE);
  }
  serve_timings_reset();
  const SipiServeTimings out = take();
  for (int i = 0; i < SIPI_PHASE_COUNT; ++i) {
    EXPECT_EQ(out.present[i], 0) << "phase " << i;
    EXPECT_EQ(out.failed[i], 0) << "phase " << i;
    EXPECT_EQ(out.dur_ns[i], 0u) << "phase " << i;
    EXPECT_EQ(out.start_ns[i], 0u) << "phase " << i;
  }
}

// A timed phase marks only its own slot present, not failed.
TEST(ServeTimings, SinglePhasePresent)
{
  serve_timings_reset();
  {
    PhaseTimer t(SIPI_PHASE_DECODE);
  }
  const SipiServeTimings out = take();
  EXPECT_EQ(out.present[SIPI_PHASE_DECODE], 1);
  EXPECT_EQ(out.failed[SIPI_PHASE_DECODE], 0);
  for (int i = 0; i < SIPI_PHASE_COUNT; ++i) {
    if (i == SIPI_PHASE_DECODE) { continue; }
    EXPECT_EQ(out.present[i], 0) << "phase " << i;
  }
}

// The phase set a resize request drives: shape + decode + encode present, the
// transform phases (rotate/quality/watermark) absent.
TEST(ServeTimings, MultiplePhasesPresent)
{
  serve_timings_reset();
  {
    PhaseTimer s(SIPI_PHASE_SHAPE);
  }
  {
    PhaseTimer d(SIPI_PHASE_DECODE);
  }
  {
    PhaseTimer e(SIPI_PHASE_ENCODE);
  }
  const SipiServeTimings out = take();
  EXPECT_EQ(out.present[SIPI_PHASE_SHAPE], 1);
  EXPECT_EQ(out.present[SIPI_PHASE_DECODE], 1);
  EXPECT_EQ(out.present[SIPI_PHASE_ENCODE], 1);
  EXPECT_EQ(out.present[SIPI_PHASE_ROTATE], 0);
  EXPECT_EQ(out.present[SIPI_PHASE_QUALITY], 0);
  EXPECT_EQ(out.present[SIPI_PHASE_WATERMARK], 0);
}

// A phase whose scope unwinds via an exception is marked failed but still present.
TEST(ServeTimings, ExceptionMarksPhaseFailed)
{
  serve_timings_reset();
  try {
    PhaseTimer t(SIPI_PHASE_ENCODE);
    throw std::runtime_error("boom");
  } catch (const std::exception &) {
    // swallowed — the timer's destructor already ran during unwind
  }
  const SipiServeTimings out = take();
  EXPECT_EQ(out.present[SIPI_PHASE_ENCODE], 1);
  EXPECT_EQ(out.failed[SIPI_PHASE_ENCODE], 1);
}

// Re-timing a phase overwrites the prior record (present stays set, failed clears
// on a subsequent clean pass).
TEST(ServeTimings, RetimeOverwrites)
{
  serve_timings_reset();
  try {
    PhaseTimer t(SIPI_PHASE_DECODE);
    throw std::runtime_error("boom");
  } catch (const std::exception &) {
  }
  ASSERT_EQ(take().failed[SIPI_PHASE_DECODE], 1);
  {
    PhaseTimer t(SIPI_PHASE_DECODE);
  }
  const SipiServeTimings out = take();
  EXPECT_EQ(out.present[SIPI_PHASE_DECODE], 1);
  EXPECT_EQ(out.failed[SIPI_PHASE_DECODE], 0);
}

// serve_timings_export tolerates a NULL out (the FFI take passes a real pointer,
// but the contract is a documented no-op on NULL).
TEST(ServeTimings, ExportNullIsNoop)
{
  serve_timings_reset();
  serve_timings_export(nullptr);
}

}// namespace
