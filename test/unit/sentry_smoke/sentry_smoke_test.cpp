/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

// Smoke test for the native sentry-native cc_library (DEV-6563, Phase 7).
//
// The inproc crash backend is configured purely by which source files the
// build selects (backend / transport / unwinder / modulefinder). A wrong pick
// compiles and links fine but silently fails to capture at runtime — exactly
// the failure mode a build-green check misses. This test exercises the runtime
// path end-to-end: sentry_init brings up the inproc backend, the libbacktrace
// unwinder captures a live stack (add_stacktrace with NULL ips), and the event
// flows through the SDK pipeline to a transport. (Full SIGSEGV crash capture is
// validated by SIPI's deployed Sentry integration, not a fork-based unit test.)

#include "gtest/gtest.h"

#include "sentry.h"

#include <atomic>
#include <cstdlib>
#include <string>

namespace {

std::atomic<int> g_envelopes{0};

void capture_transport(sentry_envelope_t *envelope, void * /*state*/)
{
  g_envelopes.fetch_add(1, std::memory_order_relaxed);
  sentry_envelope_free(envelope);
}

}// namespace

TEST(SentrySmoke, InitUnwindCaptureDeliver)
{
  const char *tmp = std::getenv("TEST_TMPDIR");
  const std::string db = std::string(tmp ? tmp : "/tmp") + "/.sentry-native-smoke";

  g_envelopes.store(0);

  sentry_options_t *options = sentry_options_new();
  sentry_options_set_dsn(options, "https://examplePublicKey@o0.ingest.sentry.io/0");
  sentry_options_set_database_path(options, db.c_str());
  sentry_options_set_transport(options, sentry_transport_new(capture_transport));

  ASSERT_EQ(sentry_init(options), 0);

  sentry_value_t event = sentry_value_new_message_event(SENTRY_LEVEL_INFO, "test", "native sentry smoke");
  // Deprecated upstream but kept deliberately: with ips==NULL it captures the
  // current stack via the configured unwinder, which is exactly what this test
  // needs to exercise libbacktrace. If a sentry bump removes it, migrate to
  // sentry_value_new_stacktrace + sentry_value_new_thread + sentry_event_add_thread.
  sentry_event_value_add_stacktrace(event, nullptr, 0);
  sentry_capture_event(event);

  ASSERT_EQ(sentry_close(), 0);

  EXPECT_GE(g_envelopes.load(), 1)
    << "inproc backend + libbacktrace unwinder + transport did not deliver the captured event";
}
