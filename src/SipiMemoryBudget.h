/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_SIPIMEMORYBUDGET_H
#define SIPI_SIPIMEMORYBUDGET_H

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>

namespace Sipi {

/// Memory budget mode: off (disabled), monitor (log only), enforce (return 503).
enum class MemoryBudgetMode { OFF, MONITOR, ENFORCE };

/// Parse mode string from config. Returns OFF for unrecognized values.
[[nodiscard]] MemoryBudgetMode parse_memory_budget_mode(const std::string &mode_str);

/// Result of a memory budget acquisition attempt.
struct MemoryBudgetResult {
  bool allowed;       ///< false if over budget and mode == ENFORCE
  bool over_budget;   ///< true if would exceed budget (logged in MONITOR mode)
  size_t used;        ///< current usage after this request
  size_t budget;      ///< total budget
};

/*!
 * Global memory budget for concurrent image decode operations.
 *
 * Tracks aggregate memory consumption across all in-flight decodes using
 * a lock-free atomic counter. Prevents OOM from multiple simultaneous
 * large image decodes.
 *
 * Thread-safety: All public methods are safe to call from any thread.
 * Uses std::atomic<size_t> with compare_exchange_weak for lock-free
 * acquire/release operations.
 */
class SipiMemoryBudget
{
public:
  SipiMemoryBudget(size_t total_budget, MemoryBudgetMode mode);

  /*!
   * Try to acquire `bytes` from the budget.
   *
   * In ENFORCE mode: returns allowed=false if acquisition would exceed budget.
   * In MONITOR mode: returns allowed=true but sets over_budget=true for logging.
   * In OFF mode: no tracking, always returns allowed=true, over_budget=false.
   *
   * @param bytes  Estimated peak memory for this decode operation
   * @return MemoryBudgetResult with decision and current state
   */
  [[nodiscard]] MemoryBudgetResult try_acquire(size_t bytes);

  /*!
   * Release `bytes` back to the budget.
   *
   * Must be called exactly once for each successful acquire.
   * Clamps to zero on underflow (defensive — should not happen in correct usage).
   */
  void release(size_t bytes);

  /// Current bytes allocated to in-flight decodes.
  [[nodiscard]] size_t used() const { return _used.load(std::memory_order_relaxed); }

  /// Configured total budget.
  [[nodiscard]] size_t budget() const { return _budget; }

  /// Current operating mode.
  [[nodiscard]] MemoryBudgetMode mode() const { return _mode; }

private:
  std::atomic<size_t> _used{0};
  size_t _budget;
  MemoryBudgetMode _mode;
};

/*!
 * RAII guard that releases memory budget on destruction.
 *
 * Ensures budget is released on all exit paths, including exceptions.
 * Non-copyable. Move-enabled for transfer of ownership.
 */
class MemoryBudgetGuard
{
public:
  /// Construct a guard. If `acquired` is false (budget was not acquired),
  /// the destructor is a no-op. Optional `on_release` callback fires
  /// after release (used for gauge updates without coupling to metrics).
  MemoryBudgetGuard(SipiMemoryBudget &budget, size_t bytes, bool acquired,
                    std::function<void()> on_release = nullptr);

  ~MemoryBudgetGuard();

  // Non-copyable
  MemoryBudgetGuard(const MemoryBudgetGuard &) = delete;
  MemoryBudgetGuard &operator=(const MemoryBudgetGuard &) = delete;

  // Movable
  MemoryBudgetGuard(MemoryBudgetGuard &&other) noexcept;
  MemoryBudgetGuard &operator=(MemoryBudgetGuard &&other) noexcept;

private:
  SipiMemoryBudget *_budget;
  size_t _bytes;
  bool _acquired;
  std::function<void()> _on_release;
};

}// namespace Sipi

#endif// SIPI_SIPIMEMORYBUDGET_H
