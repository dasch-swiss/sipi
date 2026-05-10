/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_OBSERVABILITY_SENTRY_INIT_H
#define SIPI_OBSERVABILITY_SENTRY_INIT_H

#include <string>

namespace Sipi::observability {

/*!
 * Configuration for sentry-native initialization, populated from CLI
 * options / `SIPI_SENTRY_*` environment variables before sipi loads its
 * Lua config. Empty `dsn` disables Sentry entirely.
 *
 * `release` carries the *presence flag* of the SIPI_SENTRY_RELEASE
 * environment variable, not its value: when non-empty, the release tag
 * sent to Sentry is taken from the binary's `BUILD_SCM_TAG` (the
 * version-stamped header). This matches existing behaviour preserved
 * verbatim from sipi.cpp:442-484.
 */
struct SentryConfig
{
  std::string dsn;        ///< SIPI_SENTRY_DSN
  std::string environment;///< SIPI_SENTRY_ENVIRONMENT (default "development")
  std::string release;    ///< SIPI_SENTRY_RELEASE — non-empty enables release tagging
};

/*!
 * Initialize sentry-native with the supplied configuration. No-op when
 * `cfg.dsn` is empty (Sentry stays disabled). Safe to call once per
 * process; subsequent calls would re-init the SDK.
 */
void init_sentry(const SentryConfig &cfg);

/*!
 * Close the sentry-native session, flushing any pending events. Safe to
 * call when Sentry was never initialized (becomes a no-op).
 */
void close_sentry();

}// namespace Sipi::observability

#endif// SIPI_OBSERVABILITY_SENTRY_INIT_H
