/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

// The C headers, not `<cstddef>`/`<cstdint>`: this file is pure C ABI, and the
// C++ headers only guarantee the `std::`-qualified spellings of `uint8_t` and
// `size_t` used unqualified below.
#include <stddef.h>
#include <stdint.h>

// Hand-mirrored declaration of `shim.rs`'s entry point, following the
// `src/ffi/cpp/sipi_ffi.h` convention (no cbindgen). Must stay in sync with the
// Rust signature — a mismatch is UB the compiler cannot see.
extern "C" int sipi_fuzz_parse_request(const uint8_t *data, size_t len);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  return sipi_fuzz_parse_request(data, size);
}
