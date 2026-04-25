/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Sweep `parse_iiif_uri` over every input file in the seed fuzz corpus under
// a wall-clock budget. New corpus inputs are exercised automatically.

#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "handlers/iiif_handler.hpp"

#ifndef SIPI_FUZZ_CORPUS_DIR
#error "SIPI_FUZZ_CORPUS_DIR must point at fuzz/handlers/corpus/"
#endif

namespace {

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

[[nodiscard]] std::string read_file(const std::filesystem::path &p)
{
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}// namespace

TEST(iiif_handler_corpus, every_corpus_input_parses_within_budget)
{
  using namespace handlers::iiif_handler;

  const std::filesystem::path corpus_dir{ SIPI_FUZZ_CORPUS_DIR };
  ASSERT_TRUE(std::filesystem::exists(corpus_dir)) << "corpus directory missing: " << corpus_dir;

  size_t count = 0;
  for (const auto &entry : std::filesystem::directory_iterator(corpus_dir)) {
    if (!entry.is_regular_file()) { continue; }
    ++count;
    const auto path = entry.path();
    const auto contents = read_file(path);
    const bool finished = completes_within(std::chrono::milliseconds(100),
      [contents = std::move(contents)] { (void)parse_iiif_uri(contents); });
    EXPECT_TRUE(finished) << "parse_iiif_uri did not complete within 100ms on corpus input: "
                          << path.filename().string();
  }
  EXPECT_GT(count, 0u) << "corpus directory was empty: " << corpus_dir;
}
