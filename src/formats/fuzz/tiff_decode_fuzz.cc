/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <cstddef>
#include <cstdint>

#include "codec_fuzz_harness.h"
#include "formats/SipiIOTiff.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  return sipi::fuzz::run_decode<Sipi::SipiIOTiff>(data, size, ".tif");
}
