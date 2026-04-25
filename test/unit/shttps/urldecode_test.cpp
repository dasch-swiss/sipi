/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "shttps/Connection.h"

namespace {

// Run `f` in a detached thread; return true if it completed within `timeout`.
// On timeout the worker thread is leaked; the test fails and the process exits,
// so the OS reaps the thread.
template<typename F> [[nodiscard]] bool completes_within(std::chrono::milliseconds timeout, F f)
{
  auto done = std::make_shared<std::atomic<bool>>(false);
  std::thread([done, fn = std::move(f)]() mutable {
    fn();
    done->store(true, std::memory_order_release);
  }).detach();

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (done->load(std::memory_order_acquire)) { return true; }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

constexpr auto kUrldecodeBudget = std::chrono::milliseconds(50);

}// namespace

TEST(urldecode, empty)
{
  EXPECT_EQ(shttps::urldecode(""), "");
}

TEST(urldecode, no_percent)
{
  EXPECT_EQ(shttps::urldecode("plain_ascii_string-123"), "plain_ascii_string-123");
}

TEST(urldecode, valid_hex_in_middle)
{
  EXPECT_EQ(shttps::urldecode("a%20b"), "a b");
}

TEST(urldecode, valid_hex_uppercase)
{
  EXPECT_EQ(shttps::urldecode("a%2Fb"), "a/b");
}

TEST(urldecode, multiple_valid_hex)
{
  EXPECT_EQ(shttps::urldecode("%23%2F%3F"), "#/?");
}

TEST(urldecode, invalid_hex_passthrough)
{
  EXPECT_EQ(shttps::urldecode("a%zzb"), "a%zzb");
}

TEST(urldecode, trailing_percent_completes)
{
  std::string out;
  EXPECT_TRUE(completes_within(kUrldecodeBudget, [&] { out = shttps::urldecode("lena%"); }));
  EXPECT_EQ(out, "lena%");
}

TEST(urldecode, trailing_percent_with_one_hex_char_completes)
{
  std::string out;
  EXPECT_TRUE(completes_within(kUrldecodeBudget, [&] { out = shttps::urldecode("lena%a"); }));
  EXPECT_EQ(out, "lena%a");
}

TEST(urldecode, run_of_trailing_percents_completes)
{
  const std::string input("\xf4%%%%%%%%%%%%%%%%%%%", 20);
  std::string out;
  EXPECT_TRUE(completes_within(kUrldecodeBudget, [&] { out = shttps::urldecode(input); }));
}

TEST(urldecode, percent_at_penultimate_position_completes)
{
  std::string out;
  EXPECT_TRUE(completes_within(kUrldecodeBudget, [&] { out = shttps::urldecode("ab%c"); }));
  EXPECT_EQ(out, "ab%c");
}

TEST(urldecode, form_encoded_plus_before_valid_hex)
{
  EXPECT_EQ(shttps::urldecode("a+b%20c", /*form_encoded=*/true), "a b c");
}

TEST(urldecode, form_encoded_plus_before_invalid_hex)
{
  EXPECT_EQ(shttps::urldecode("a+b%zzc", /*form_encoded=*/true), "a b%zzc");
}

TEST(urldecode, form_encoded_no_percent_no_plus_changes)
{
  EXPECT_EQ(shttps::urldecode("plain", /*form_encoded=*/true), "plain");
}
