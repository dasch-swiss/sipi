/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "observability/sentry_init.h"

#include <atomic>
#include <string>

#include <sentry.h>

#include "generated/SipiVersion.h"

namespace Sipi::observability {

void init_sentry(const SentryConfig &cfg)
{
  if (cfg.dsn.empty()) { return; }

  sentry_options_t *options = sentry_options_new();
  sentry_options_set_dsn(options, cfg.dsn.c_str());
  sentry_options_set_database_path(options, "/tmp/.sentry-native");

  sentry_options_set_symbolize_stacktraces(options, true);

  if (!cfg.release.empty()) {
    // Release tag is the build-stamped BUILD_SCM_TAG, not the env-var value
    // — preserved verbatim from the original sipi.cpp behaviour.
    std::string sentryReleaseTag = std::string(BUILD_SCM_TAG);
    sentry_options_set_release(options, sentryReleaseTag.c_str());
  }

  if (!cfg.environment.empty()) {
    sentry_options_set_environment(options, cfg.environment.c_str());
  } else {
    sentry_options_set_environment(options, "development");
  }

  sentry_options_set_debug(options, 0);

  // Sampling rate for transactions.
  sentry_options_set_traces_sample_rate(options, 0.1);

  sentry_init(options);
}

void close_sentry()
{
  // Idempotent: only the first caller invokes sentry_close(). sentry_close()
  // blocks until the background transport thread has drained/joined — it must
  // complete before ~LibraryInitialiser runs curl_global_cleanup() at process
  // exit (see the std::atexit registration in sipi.cpp). It is NULL-guarded in
  // sentry-native (no-op when never initialised), so the flag mainly prevents
  // a redundant shutdown attempt and its spurious warning on repeated calls.
  static std::atomic_flag closed = ATOMIC_FLAG_INIT;
  if (closed.test_and_set()) { return; }
  sentry_close();
}

}// namespace Sipi::observability
