/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "SipiRateLimiter.h"

#include <algorithm>

namespace Sipi {

RateLimitMode parse_rate_limit_mode(const std::string &mode_str)
{
  if (mode_str == "monitor") return RateLimitMode::MONITOR;
  if (mode_str == "enforce") return RateLimitMode::ENFORCE;
  return RateLimitMode::OFF;
}

SipiRateLimiter::SipiRateLimiter(unsigned window_seconds,
  size_t max_pixels,
  RateLimitMode mode,
  size_t pixel_threshold)
  : _window_seconds(window_seconds),
    _max_pixels(max_pixels),
    _mode(mode),
    _pixel_threshold(pixel_threshold)
{
}

RateLimitResult SipiRateLimiter::check_and_record(const std::string &client_id, size_t pixels)
{
  // Requests below the pixel threshold are free (e.g., tile requests)
  if (pixels < _pixel_threshold) {
    return { true, false, 0, _max_pixels, 0 };
  }

  auto now = std::chrono::steady_clock::now();

  std::scoped_lock lock(_mutex);

  _check_count++;
  // R29: Full map sweep every 1000 checks
  if (_check_count % 1000 == 0) {
    sweep_clients();
  }

  auto &records = _clients[client_id];
  cleanup_client(records, now);

  // Sum current window consumption
  size_t consumed = 0;
  for (const auto &r : records) {
    consumed += r.pixels;
  }

  bool over_budget = (consumed + pixels) > _max_pixels;

  // R27: Optimistic deduction — record before processing
  records.push_back({ now, pixels });
  consumed += pixels;

  // Compute retry_after: seconds until oldest record expires
  unsigned retry_after = 0;
  if (over_budget && !records.empty()) {
    auto window = std::chrono::seconds(_window_seconds);
    auto oldest_expires = records.front().timestamp + window;
    if (oldest_expires > now) {
      auto wait = std::chrono::duration_cast<std::chrono::seconds>(oldest_expires - now).count();
      retry_after = std::max(1u, static_cast<unsigned>(wait));
    } else {
      retry_after = 1;
    }
  }

  // In monitor mode, always allow (but report over_budget for logging)
  bool allowed = !over_budget || (_mode == RateLimitMode::MONITOR);

  return { allowed, over_budget, consumed, _max_pixels, retry_after };
}

size_t SipiRateLimiter::tracked_clients() const
{
  std::scoped_lock lock(_mutex);
  return _clients.size();
}

void SipiRateLimiter::cleanup_client(std::deque<Record> &records,
  std::chrono::steady_clock::time_point now) const
{
  auto cutoff = now - std::chrono::seconds(_window_seconds);
  while (!records.empty() && records.front().timestamp < cutoff) {
    records.pop_front();
  }
}

void SipiRateLimiter::sweep_clients()
{
  auto now = std::chrono::steady_clock::now();
  for (auto it = _clients.begin(); it != _clients.end();) {
    cleanup_client(it->second, now);
    if (it->second.empty()) {
      it = _clients.erase(it);
    } else {
      ++it;
    }
  }
}

}// namespace Sipi
