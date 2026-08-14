/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * Process-startup helpers shared by the two production entry points that run at
 * process start: `sipi_init` (the server-mode engine install, `ffi/init.cpp`)
 * and `sipi_cli_main` (the CLI dispatch, `cli/cli_app.cpp`). They live here in
 * the seam package — not in `cli/` — so the production `init.cpp` can reach them
 * without a `src/cli` → oracle dependency; the oracle CLI reaches them the same
 * way (the dependency runs cli → ffi, never the reverse).
 */
#ifndef SIPI_FFI_STARTUP_H
#define SIPI_FFI_STARTUP_H

#include <cstddef>

namespace Sipi::ffi {

/*!
 * Singleton that performs the one-time global init/cleanup of the libraries the
 * decode pipeline needs: libcurl, the Exiv2 XMP parser (registering the `sipi`
 * namespace under `xmp_mutex`), and libtiff. Some of these are not thread-safe
 * to initialise, so it is done once on the main thread. Constructed the first
 * time `instance()` is called (thread-safe C++11 function-static) and torn down
 * at process exit.
 */
class LibraryInitialiser
{
public:
  /*! The singleton instance (constructed on first call). */
  static LibraryInitialiser &instance();

private:
  LibraryInitialiser();
  ~LibraryInitialiser();
};

/*!
 * Detect the memory available to this process, in bytes: the cgroup limit
 * (v2 then v1) when running under one, else the host `MemTotal` (Linux) or
 * `hw.memsize` (macOS). Returns 0 if detection fails, so the caller can fall
 * back to a fixed default.
 */
[[nodiscard]] std::size_t detect_available_memory();

}// namespace Sipi::ffi

#endif// SIPI_FFI_STARTUP_H
