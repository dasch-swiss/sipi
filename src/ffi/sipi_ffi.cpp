/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/sipi_ffi.h"

#include <utility>

#include "ffi/serve_response.h"

extern "C" {

int sipi_serve_file(const char *resolved_path, const char *range, const SipiResponse *resp)
{
  // The whole body is wrapped so no C++ exception escapes into the caller (UB
  // across extern "C"). decide_serve_file runs every fallible step before the
  // response is committed, so a failure is rendered as a clean status code by
  // the caller; apply is the only code that touches the response callbacks.
  return Sipi::ffi::sipi_guard([&] {
    auto decision = Sipi::ffi::decide_serve_file(resolved_path, range);
    if (!decision) { return static_cast<int>(decision.error()); }
    Sipi::ffi::apply(std::move(*decision), *resp);
    return static_cast<int>(Sipi::ffi::SipiStatus::Ok);
  });
}

}// extern "C"
