/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * \brief Thin entry point for the C++ `sipi` binary.
 *
 * The CLI11 app and every subcommand body live in `//src/cli:cli_app` behind
 * the `sipi_cli_main` FFI entry (`ffi/sipi_ffi.h`). Keeping `main` in its own
 * translation unit lets the Rust HTTP shell (strangler-fig Phase C) link
 * `cli_app` and call `sipi_cli_main` without colliding with this `main`. The
 * `sipi_cli_main` entry owns process-global init (Sentry, library init) and
 * dispatch, and returns the exit code rather than calling `exit()`.
 */
#include "ffi/sipi_ffi.h"

int main(int argc, char **argv) { return sipi_cli_main(argc, argv); }
