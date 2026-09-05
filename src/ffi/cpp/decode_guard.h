/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * The single decode-guard chokepoint shared by both engine decode entry
 * points: the IIIF serve path (`serve_image.cpp`) and the Lua-driven
 * `SipiImage.new()` path (`image_handle.cpp`, S2-20). Both funnel through
 * `run_with_deadline` for the DEV-7080 decode-hang watchdog and through
 * `acquire_full_lane_budget` + `run_guarded_decode` for the full-lane memory
 * budget, so there is exactly one place that estimates→acquires→guards→runs
 * a decode under deadline, for either caller.
 */
#ifndef SIPI_FFI_DECODE_GUARD_H
#define SIPI_FFI_DECODE_GUARD_H

#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <optional>
#include <thread>
#include <type_traits>

#include "observability/metrics.h"
#include "throttling/SipiMemoryBudget.h"

namespace Sipi::ffi {

// Runs `producer()` on a joinable worker thread bounded by `timeout`. Returns
// the producer's result if it finished in time; `std::nullopt` on timeout — in
// which case the worker is DETACHED (an unpatchable Kakadu decode hang keeps
// running until the process restarts, DEV-7080) and `wedged_threads` is
// incremented. `producer` must capture its inputs BY VALUE and return BY
// VALUE: on timeout the caller's stack unwinds while the detached worker still
// owns the packaged_task's shared state, so the worker only ever writes into
// heap it co-owns (no use-after-free).
template<class F> std::optional<std::invoke_result_t<F>> run_with_deadline(std::chrono::milliseconds timeout, F producer)
{
  using T = std::invoke_result_t<F>;
#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
  // Run the producer INLINE under ASan — no worker thread, no join. ASan's
  // thread registry falsely reports "Joining already joined thread, aborting"
  // when a short-lived worker thread's slot ID is reused across the Rust
  // shell's heavy thread-creation history (the same false positive the Kakadu
  // worker-pool guards in SipiIOJ2k.cpp disable MT for). The deadline is a
  // production availability guard for the unpatchable Kakadu decode hang
  // (DEV-7080), and ASan builds never ship, so dropping the watchdog thread
  // under ASan only forgoes a test-only timeout while keeping the decode itself
  // fully exercised under the sanitizer.
  (void)timeout;
  return std::optional<T>(producer());
#else
  auto task = std::make_shared<std::packaged_task<T()>>(std::move(producer));
  std::future<T> fut = task->get_future();
  std::thread worker([task] { (*task)(); });
  if (fut.wait_for(timeout) == std::future_status::ready) {
    worker.join();
    return fut.get();
  }
  worker.detach();
  observability::Metrics::instance().wedged_threads.Increment();
  return std::nullopt;
#endif
}

// The outcome of consulting the full-lane memory budget for one decode
// (S2-20 / the structural follow-up that unified the two acquire sites).
struct FullLaneAcquisition
{
  // False only when the budget was consulted (installed + this decode's
  // estimate reached the full-lane threshold) AND `ADVANCED` mode refused
  // the request — the caller must reject the request without decoding.
  bool allowed = true;
  // True iff a budget was actually installed and this decode's estimate
  // reached `large_decode_threshold_bytes` — i.e. `result` and `guard` are
  // meaningful. False means the decode bypassed the budget entirely (tile
  // decode, or no budget installed), same as before.
  bool consulted = false;
  MemoryBudgetResult result{};
  // Engaged iff `consulted && allowed`. Move this into `run_guarded_decode`
  // to charge the budget across the decode; an early return before that
  // (e.g. request cancellation) releases it normally on scope exit.
  std::optional<MemoryBudgetGuard> guard;
};

// Estimates→acquires: the full-lane budget gate shared by both decode entry
// points. `estimated_bytes` is caller-computed — the two callers'
// `estimate_peak_memory` inputs differ (the IIIF path folds in the request's
// rotation angle and output-ICC need; `SipiImage.new()` does not), so the
// dimension/estimate math stays with the caller and this function starts at
// `try_acquire`. `on_release` mirrors `MemoryBudgetGuard`'s own optional
// callback (e.g. the serve path's `decode_memory_used_bytes` gauge refresh);
// omit it for callers with no such side effect.
[[nodiscard]] FullLaneAcquisition acquire_full_lane_budget(SipiMemoryBudget *memory_budget,
  std::size_t large_decode_threshold_bytes,
  std::size_t estimated_bytes,
  std::function<void()> on_release = nullptr);

// The result of a guarded decode: `value` is the producer's result and
// `guard` is the SAME budget guard passed in, still armed — the caller keeps
// charging the budget for as long as it needs the decoded buffer (e.g.
// through a subsequent encode). `std::nullopt` on deadline timeout, in which
// case `guard` is NOT returned to the caller: it stays captured inside the
// detached worker's packaged_task and keeps the budget charged until that
// worker eventually finishes (or the process restarts, DEV-7080). The
// caller must not construct a replacement guard for the same request — doing
// so would double-charge the budget for memory the wedged thread still holds.
template<class T> struct GuardedDecode
{
  T value;
  std::optional<MemoryBudgetGuard> guard;
};

// Runs `producer` under `run_with_deadline` while carrying `guard` (if any)
// for the duration. `guard` is moved INTO the closure that runs on the
// worker thread — co-owned by the detachable work, not bound to this
// caller's stack frame — so a timeout leaves the charge with the wedged
// thread instead of refunding memory that thread still holds.
template<class F>
std::optional<GuardedDecode<std::invoke_result_t<F>>> run_guarded_decode(std::chrono::milliseconds timeout,
  std::optional<MemoryBudgetGuard> guard,
  F producer)
{
  using T = std::invoke_result_t<F>;
  return run_with_deadline(timeout,
    [guard = std::move(guard), producer = std::move(producer)]() mutable -> GuardedDecode<T> {
      return GuardedDecode<T>{ producer(), std::move(guard) };
    });
}

}// namespace Sipi::ffi

#endif// SIPI_FFI_DECODE_GUARD_H
