/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

// Health subcommand unit tests.
//
// The OK path (server returns 200 → exit 0) is covered end-to-end by the Rust
// e2e suite (`test/e2e-rust/tests/health.rs`) and the Docker smoke test, both
// of which have a live server to probe. Here we cover the NOK path
// hermetically: with nothing listening on the target port, `cmd_health` must
// return EXIT_FAILURE.
//
// Unlike the CLI binary, this test does not go through `LibraryInitialiser`,
// so it performs `curl_global_init` / `curl_global_cleanup` itself.

#include <gtest/gtest.h>

#include <cstdlib>

#include <curl/curl.h>

#include "cli/commands/health.h"

namespace {

class HealthTest : public ::testing::Test
{
protected:
  void SetUp() override { curl_global_init(CURL_GLOBAL_ALL); }
  void TearDown() override { curl_global_cleanup(); }
};

// Nothing listens on port 1 → connection refused → unhealthy.
TEST_F(HealthTest, ReturnsFailureWhenNothingListening)
{
  EXPECT_EQ(Sipi::cli::cmd_health(Sipi::cli::HealthArgs{ .port = 1 }), EXIT_FAILURE);
}

}// namespace
