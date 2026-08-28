/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gtest/gtest.h"

#include "SipiValueError.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

namespace {

using Sipi::ErrorCode;
using Sipi::HttpStatusClass;
using Sipi::MetricHint;
using Sipi::policy_for;
using Sipi::Result;
using Sipi::SentryPolicy;
using Sipi::SipiValueError;

// docs/src/development/error-model.md's policy table is the source of
// truth; every ErrorCode row is asserted individually here rather than just
// checking that policy_for returns "something".
TEST(PolicyFor, MatchesErrorModelTable)
{
  {
    const auto p = policy_for(ErrorCode::kDecodeFailed);
    EXPECT_EQ(p.http_status_class, HttpStatusClass::kInternalError);
    EXPECT_EQ(p.sentry_policy, SentryPolicy::kReport);
    EXPECT_EQ(p.metric_hint, MetricHint::kNone);
  }
  {
    const auto p = policy_for(ErrorCode::kUnsupportedFormat);
    EXPECT_EQ(p.http_status_class, HttpStatusClass::kInternalError);
    EXPECT_EQ(p.sentry_policy, SentryPolicy::kReport);
    EXPECT_EQ(p.metric_hint, MetricHint::kNone);
  }
  {
    const auto p = policy_for(ErrorCode::kMalformedInput);
    EXPECT_EQ(p.http_status_class, HttpStatusClass::kInternalError);
    EXPECT_EQ(p.sentry_policy, SentryPolicy::kReport);
    EXPECT_EQ(p.metric_hint, MetricHint::kNone);
  }
  {
    const auto p = policy_for(ErrorCode::kWriteFailed);
    EXPECT_EQ(p.http_status_class, HttpStatusClass::kInternalError);
    EXPECT_EQ(p.sentry_policy, SentryPolicy::kReport);
    EXPECT_EQ(p.metric_hint, MetricHint::kNone);
  }
  {
    const auto p = policy_for(ErrorCode::kClientAbort);
    EXPECT_EQ(p.http_status_class, HttpStatusClass::kInternalError);
    EXPECT_EQ(p.sentry_policy, SentryPolicy::kSkip);
    EXPECT_EQ(p.metric_hint, MetricHint::kClientDisconnected);
  }
  {
    const auto p = policy_for(ErrorCode::kShapeProbeFailed);
    EXPECT_EQ(p.http_status_class, HttpStatusClass::kInternalError);
    EXPECT_EQ(p.sentry_policy, SentryPolicy::kReport);
    EXPECT_EQ(p.metric_hint, MetricHint::kNone);
  }
  {
    const auto p = policy_for(ErrorCode::kMetadataParseFailed);
    EXPECT_EQ(p.http_status_class, HttpStatusClass::kInternalError);
    EXPECT_EQ(p.sentry_policy, SentryPolicy::kReport);
    EXPECT_EQ(p.metric_hint, MetricHint::kNone);
  }
}

TEST(PolicyFor, IsUsableAtCompileTime)
{
  static_assert(policy_for(ErrorCode::kDecodeFailed).sentry_policy == SentryPolicy::kReport);
  static_assert(policy_for(ErrorCode::kClientAbort).metric_hint == MetricHint::kClientDisconnected);
  SUCCEED();
}

// redact_paths (util/PathRedact.h) collapses a whitespace-delimited token
// containing '/' down to the substring after its last '/', preserving any
// leading run of quote characters on the token.
TEST(SipiValueErrorTest, ClientMessageIsPathRedactedAndHasNoSourceLocation)
{
  const SipiValueError err{ ErrorCode::kDecodeFailed, R"(Cannot read file "/srv/images/sub/foo.jp2": broken)" };

  const std::string msg = err.client_message();
  EXPECT_EQ(msg, R"(Cannot read file "foo.jp2": broken)");
  EXPECT_EQ(msg.find(__FILE__), std::string::npos);
}

TEST(SipiValueErrorTest, DiagnosticMessageHasSourceLocationAndIsNotRedacted)
{
  const SipiValueError err{ ErrorCode::kDecodeFailed, "Cannot read file \"/srv/images/sub/foo.jp2\": broken" };

  const std::string msg = err.diagnostic_message();
  EXPECT_NE(msg.find(__FILE__), std::string::npos);
  EXPECT_NE(msg.find("/srv/images/sub/foo.jp2"), std::string::npos);
  EXPECT_NE(msg.find("Sipi image error at ["), std::string::npos);
}

// SipiImageError is the reference shape for this wording (see
// src/image/cpp/SipiImageError.h's message()/to_string()); //src/error must
// not depend on //src/image, so the expected text is asserted literally
// instead of constructed via SipiImageError side by side.
TEST(SipiValueErrorTest, ErrnumIsSplicedIntoClientMessage)
{
  const int errnum = ENOENT;
  const SipiValueError err{ ErrorCode::kDecodeFailed, "failed", errnum };

  const std::string expected = std::string("(system error: ") + std::strerror(errnum) + "): failed";
  EXPECT_EQ(err.client_message(), expected);
}

TEST(SipiValueErrorTest, ErrnumIsSplicedIntoDiagnosticMessage)
{
  const int errnum = ENOENT;
  const SipiValueError err{ ErrorCode::kDecodeFailed, "failed", errnum };

  const std::string msg = err.diagnostic_message();
  const std::string expected_suffix = std::string(" (system error: ") + std::strerror(errnum) + "): failed";
  EXPECT_NE(msg.find(expected_suffix), std::string::npos);
}

TEST(ResultTest, SuccessRoundTrip)
{
  Result<int> r = 42;
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 42);
}

TEST(ResultTest, ErrorRoundTripSurvivesMove)
{
  Result<int> r = std::unexpected(SipiValueError{ ErrorCode::kMalformedInput, "bad input" });
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code(), ErrorCode::kMalformedInput);

  Result<int> moved = std::move(r);
  ASSERT_FALSE(moved.has_value());
  EXPECT_EQ(moved.error().code(), ErrorCode::kMalformedInput);
  EXPECT_EQ(moved.error().client_message(), "bad input");
}

}// namespace
