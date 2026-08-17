/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_SIPIMEMORYBUDGET_H
#define SIPI_SIPIMEMORYBUDGET_H

#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace Sipi {

/// Admission mode. Under `BASIC` the memory budget only shadow-counts what the
/// advanced tier *would* shed; under `ADVANCED` it rejects over budget with
/// 503/413. There is no "off": the budget is always accounted so the shadow
/// counters are always available to size the full lane before the advanced flip.
enum class AdmissionMode { BASIC, ADVANCED };

/// Parse the admission-mode string from config. Returns `std::nullopt` for an
/// unrecognized value so the caller can fall back to the `BASIC` default.
[[nodiscard]] std::optional<AdmissionMode> parse_admission_mode(const std::string &mode_str);

/// Result of a memory budget acquisition attempt.
struct MemoryBudgetResult
{
  bool allowed;              ///< false if over budget and mode == ADVANCED
  bool over_budget;          ///< true if this acquire pushed usage over budget (shadow-counted in BASIC)
  bool exceeds_budget_alone; ///< true if this request's estimate alone exceeds the budget (permanently unservable → 413, not 503)
  size_t used;               ///< current usage after this request
  size_t budget;             ///< total budget (the full lane's byte cap)
};

/*!
 * Full-lane memory budget for concurrent image decode operations.
 *
 * Tracks aggregate memory consumption across all in-flight full-lane decodes
 * using a lock-free atomic counter. Prevents OOM from multiple simultaneous
 * large image decodes. Tile decodes (below the large-decode threshold) bypass
 * the budget entirely and are never charged.
 *
 * Thread-safety: All public methods are safe to call from any thread.
 * Uses std::atomic<size_t> with compare_exchange_weak for lock-free
 * acquire/release operations.
 */
class SipiMemoryBudget
{
public:
  SipiMemoryBudget(size_t total_budget, AdmissionMode mode);

  /*!
   * Try to acquire `bytes` from the budget.
   *
   * In ADVANCED mode: returns allowed=false if acquisition would exceed budget.
   * In BASIC mode: returns allowed=true but sets over_budget=true for logging.
   * `exceeds_budget_alone` is set independently of mode whenever this single
   * request's estimate is larger than the whole budget (a permanently-unservable
   * request the caller answers with 413, not a transient 503).
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
  [[nodiscard]] AdmissionMode mode() const { return _mode; }

private:
  std::atomic<size_t> _used{0};
  size_t _budget;
  AdmissionMode _mode;
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
