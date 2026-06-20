/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_SIPIRATELIMITER_H
#define SIPI_SIPIRATELIMITER_H

#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Sipi {

/// Rate limiter mode: off (disabled), monitor (log only), enforce (return 429).
enum class RateLimitMode { OFF, MONITOR, ENFORCE };

/// Parse mode string from config. Returns OFF for unrecognized values.
[[nodiscard]] RateLimitMode parse_rate_limit_mode(const std::string &mode_str);

/// Result of a rate limit check.
struct RateLimitResult {
  bool allowed;         ///< true if request is allowed (under budget or monitor mode)
  bool over_budget;     ///< true if client exceeded their pixel budget
  size_t pixels_consumed; ///< total pixels consumed in current window
  size_t budget;        ///< the configured max pixels budget
  unsigned retry_after; ///< seconds until budget available (0 if allowed)
};

/*!
 * Per-client sliding-window rate limiter based on pixel budget.
 *
 * Each client has a deque of (timestamp, pixels) records. On each check,
 * records outside the sliding window are removed, and the total is compared
 * to the budget. Thread-safe via a single mutex.
 */
class SipiRateLimiter
{
public:
  /*!
   * Construct a rate limiter.
   *
   * @param window_seconds  Sliding window duration in seconds
   * @param max_pixels      Maximum pixels per client per window
   * @param mode            Operating mode (off/monitor/enforce)
   * @param pixel_threshold Requests below this pixel count are free (exempt tiles)
   */
  SipiRateLimiter(unsigned window_seconds,
    size_t max_pixels,
    RateLimitMode mode,
    size_t pixel_threshold);

  /*!
   * Check and record a request's pixel consumption.
   * Budget is deducted optimistically (before processing).
   *
   * @param client_id  Client identifier (IP or custom header value)
   * @param pixels     Number of pixels in this request
   * @return RateLimitResult with decision and metadata
   */
  [[nodiscard]] RateLimitResult check_and_record(const std::string &client_id, size_t pixels);

  /// Get the current number of tracked clients (for monitoring).
  [[nodiscard]] size_t tracked_clients() const;

  /// Get the current mode.
  [[nodiscard]] RateLimitMode mode() const { return _mode; }

private:
  struct Record {
    std::chrono::steady_clock::time_point timestamp;
    size_t pixels;
  };

  unsigned _window_seconds;
  size_t _max_pixels;
  RateLimitMode _mode;
  size_t _pixel_threshold;
  size_t _check_count{0};

  mutable std::mutex _mutex;
  std::unordered_map<std::string, std::deque<Record>> _clients;

  /// Remove expired records from a client's deque.
  void cleanup_client(std::deque<Record> &records,
    std::chrono::steady_clock::time_point now) const;

  /// Sweep all clients, removing empty entries. Called every 1000 checks.
  void sweep_clients();
};

}// namespace Sipi

#endif// SIPI_SIPIRATELIMITER_H
