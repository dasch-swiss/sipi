/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_CLI_COMMANDS_HEALTH_H
#define SIPI_CLI_COMMANDS_HEALTH_H

namespace Sipi::cli {

/*!
 * Arguments for `sipi health`.
 *
 * The probe target is always `http://127.0.0.1:<port>/health` — the host
 * (loopback, no resolver dependency) and the path are fixed; only the port
 * is configurable. A separate `sipi health` process cannot know whether the
 * running server got its port from a config file, an env var, or
 * `--serverport`, so the caller (who configured the server) passes the port
 * it knows. Default `1024` matches the conventional sipi port and the image's
 * `exposed_ports`.
 */
struct HealthArgs
{
  int port = 1024;
};

/*!
 * Run `sipi health`: GET `http://127.0.0.1:<port>/health` and map the result
 * to a process exit code, following the Docker/Swarm healthcheck convention.
 *
 *   - `EXIT_SUCCESS` (0) — the endpoint returned HTTP 200 (healthy).
 *   - `EXIT_FAILURE` (1) — connection refused, timeout, or any non-200
 *                          response (unhealthy).
 */
[[nodiscard]] int cmd_health(const HealthArgs &args);

}// namespace Sipi::cli

#endif
