/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "SipiMemoryBudget.h"

#include <algorithm>
#include <cctype>

namespace Sipi {

MemoryBudgetMode parse_memory_budget_mode(const std::string &mode_str)
{
  // Case-insensitive comparison
  std::string lower = mode_str;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

  if (lower == "enforce") return MemoryBudgetMode::ENFORCE;
  if (lower == "monitor") return MemoryBudgetMode::MONITOR;
  return MemoryBudgetMode::OFF;
}

SipiMemoryBudget::SipiMemoryBudget(size_t total_budget, MemoryBudgetMode mode)
  : _budget(total_budget), _mode(mode)
{}

MemoryBudgetResult SipiMemoryBudget::try_acquire(size_t bytes)
{
  if (_mode == MemoryBudgetMode::OFF) {
    return {.allowed = true, .over_budget = false, .used = 0, .budget = _budget};
  }

  if (bytes == 0) {
    return {.allowed = true, .over_budget = false, .used = _used.load(std::memory_order_relaxed), .budget = _budget};
  }

  // Lock-free acquire via compare_exchange_weak loop
  size_t current = _used.load(std::memory_order_relaxed);
  while (true) {
    // Saturate on overflow (defensive — prevents wraparound in monitor mode)
    size_t desired = (bytes <= SIZE_MAX - current) ? (current + bytes) : SIZE_MAX;
    bool would_exceed = desired > _budget;

    if (would_exceed && _mode == MemoryBudgetMode::ENFORCE) {
      return {.allowed = false, .over_budget = true, .used = current, .budget = _budget};
    }

    // In MONITOR mode or within budget: try to acquire
    if (_used.compare_exchange_weak(current, desired, std::memory_order_acq_rel, std::memory_order_relaxed)) {
      return {.allowed = true, .over_budget = would_exceed, .used = desired, .budget = _budget};
    }
    // CAS failed — `current` has been updated by compare_exchange_weak, retry
  }
}

void SipiMemoryBudget::release(size_t bytes)
{
  if (_mode == MemoryBudgetMode::OFF || bytes == 0) {
    return;
  }

  // Clamp to zero on underflow (defensive)
  size_t current = _used.load(std::memory_order_relaxed);
  while (true) {
    size_t desired = (current >= bytes) ? (current - bytes) : 0;
    if (_used.compare_exchange_weak(current, desired, std::memory_order_acq_rel, std::memory_order_relaxed)) {
      return;
    }
  }
}

// --- MemoryBudgetGuard ---

MemoryBudgetGuard::MemoryBudgetGuard(SipiMemoryBudget &budget, size_t bytes, bool acquired,
                                     std::function<void()> on_release)
  : _budget(&budget), _bytes(bytes), _acquired(acquired), _on_release(std::move(on_release))
{}

MemoryBudgetGuard::~MemoryBudgetGuard()
{
  if (_acquired && _budget != nullptr) {
    _budget->release(_bytes);
    try {
      if (_on_release) _on_release();
    } catch (...) {
      // Swallow exceptions — destructor must not throw.
    }
  }
}

MemoryBudgetGuard::MemoryBudgetGuard(MemoryBudgetGuard &&other) noexcept
  : _budget(other._budget), _bytes(other._bytes), _acquired(other._acquired),
    _on_release(std::move(other._on_release))
{
  other._acquired = false;
  other._budget = nullptr;
}

MemoryBudgetGuard &MemoryBudgetGuard::operator=(MemoryBudgetGuard &&other) noexcept
{
  if (this != &other) {
    // Release current resources
    if (_acquired && _budget != nullptr) {
      _budget->release(_bytes);
      try {
        if (_on_release) _on_release();
      } catch (...) {}
    }
    _budget = other._budget;
    _bytes = other._bytes;
    _acquired = other._acquired;
    _on_release = std::move(other._on_release);
    other._acquired = false;
    other._budget = nullptr;
  }
  return *this;
}

}// namespace Sipi
