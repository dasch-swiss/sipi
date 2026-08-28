/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_CLI_COMMANDS_VERIFY_H
#define SIPI_CLI_COMMANDS_VERIFY_H

#include <string>

namespace Sipi::cli {

/*!
 * Verify mode (DEV-6537 / DEV-6540).
 *
 * Per ADR-0009 / ADR-0010 the verify surface is tiered:
 *
 *   - `Generic`      — `sipi verify <file>`. Generic decoder-coverage
 *                       check (RDU sanity use case): can SIPI open and
 *                       decode this file? No metadata assertions.
 *   - `AccessFile`   — `sipi verify access-file <file>`. The above
 *                       plus: the file must NOT carry an Essentials
 *                       packet (its presence indicates a Service File
 *                       misclassified as an Access File).
 *   - `ServiceFile`  — `sipi verify service-file <file>`. The above
 *                       plus: the file MUST carry an Essentials
 *                       packet, the recomputed pixel hash matches
 *                       `data_chksum`, and image-shape fields are
 *                       consistent with the codec output.
 */
enum class VerifyMode {
  Generic,
  AccessFile,
  ServiceFile,
};

struct VerifyArgs
{
  VerifyMode mode = VerifyMode::Generic;
  std::string input_path;
  bool json_output = false;
};

/*!
 * Run `sipi verify`. Exit codes:
 *
 *   - `EXIT_SUCCESS` — all assertions for the requested mode passed.
 *   - `EXIT_FAILURE` — decoder error, missing/extra Essentials packet,
 *                      pixel-hash mismatch, or shape inconsistency.
 *
 * On a `ServiceFile` pixel-hash mismatch the command also emits
 * `log_err(...)` so the operator gets the diagnostic immediately
 * (a future change can wire this through the
 * `sipi_essentials_hash_mismatch_total{format}` Prometheus counter).
 */
[[nodiscard]] int cmd_verify(const VerifyArgs &args);

}// namespace Sipi::cli

#endif
