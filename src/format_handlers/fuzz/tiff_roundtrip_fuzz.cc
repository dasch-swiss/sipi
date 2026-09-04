/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <tiffio.h>

#include "codec_fuzz_harness.h"
#include "format_handlers/SipiIOTiff.h"

namespace {

// Fuzz-target-only: libtiff's default warning handler prints to stderr, and
// malformed fuzz inputs trigger it millions of times per run, which is what
// silences libtiff's warning flood so the fuzz log stays readable
// (production logging via SipiIOTiff is unchanged). The counter is not
// currently inspected; it exists so a future run can assert on warning
// volume without reintroducing the print flood.
std::atomic<std::uint64_t> gWarningCount{0};

void countWarning(const char * /*module*/, const char * /*fmt*/, va_list /*ap*/) { gWarningCount.fetch_add(1, std::memory_order_relaxed); }

}// namespace

extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
  TIFFSetWarningHandler(countWarning);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  return sipi::fuzz::run_roundtrip<Sipi::SipiIOTiff>(data, size, ".tif");
}
