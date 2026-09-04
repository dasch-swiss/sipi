---
title: JP2 decode watchdog — mechanism design (S2-13, DEV-7080)
date: 2026-09-04
author: Ivan Subotic
status: approved
repositories: []
---

# JP2 decode watchdog — mechanism design (S2-13, DEV-7080)

## Problem

A crafted JP2 codestream (DEV-7080; reproducers pinned at
`test/_test_data/images/hang/dev7080_read_shape_hang.jp2` and
`nightly_j2k_read_shape_hang.jp2`) drives Kakadu into an unbounded loop inside
`SipiImage::read_shape` / `SipiImage::read`. The decode never returns. Under the
Rust shell's bounded two-lane pool (`SIPI_NTHREADS` lanes), each such request
pins one lane forever; a handful of hostile requests wedge the whole pool into a
silent outage. Kakadu is license-gated and cannot be patched, so the loop cannot
be fixed at its source.

## Options considered

**(a) Structural box-chain pre-pass.** Walk the JP2 box chain and reject
zero-length / degenerate boxes before Kakadu opens the file. This is genuine
input validation at the true boundary, but it covers only the specific box shape
the fuzzer happened to find. Any other codestream construction that loops inside
Kakadu's entropy or wavelet stages sails past a box-shape check. It is one layer
that does not generalise to the failure *class* (an unbounded decode), only to
one instance.

**(b) Wall-clock deadline at the FFI seam.** Run the decode under a wall-clock
deadline; when `read_shape` / `read` exceeds it, fail the request. This is an
availability guard over a library that cannot be patched — not input validation —
and it generalises to every unbounded-decode instance, known or not. Its cost is
that a wedged Kakadu thread has no clean join: aborting the request leaks the
thread until process restart.

## Decision — (b), a seam deadline

Exactly one layer, per the plan's own rule (`CLAUDE.md`: no defense-in-depth).
(b) is chosen because the maintainer ruling already names the seam and because
(a) would only shield the one box shape the fuzzer found while leaving the
failure class open. Applying the single deadline mechanism to *both* decode calls
the seam makes (`read_shape` at `serve_image.cpp` and the full `read`) is not two
layers — it is the one mechanism covering the two entry points at the same seam.

## Mechanism

`build_image_response` (`src/ffi/cpp/serve_image.cpp`) runs each decode call
through a deadline helper:

- The decode runs on a joinable `std::thread` driving a
  `std::shared_ptr<std::packaged_task<T()>>`. The caller waits on the task's
  future with `wait_for(deadline)`.
- **Ready within the deadline:** join the thread and take the value. Normal path,
  no leak.
- **Timeout:** `detach()` the thread (the wedged decode keeps running), increment
  the `wedged_threads` gauge, and return `SipiStatus::InternalError` (500). The
  hang is deterministic for a given file — the same input wedges on every
  request — so it is not retryable; 500, not a 503 with `Retry-After`. The seam
  reports it through the existing Sentry side channel with phase `read`.

### Lifetime safety (no use-after-free on a leaked thread)

The producer lambda captures its inputs **by value** (the `infile` string is
copied; `region` / `size` are `shared_ptr` copies) and returns its result **by
value** into the `packaged_task`'s shared state. That shared state is co-owned by
the detached worker through the captured `shared_ptr`, so a timed-out worker that
finally unwinds writes only into heap it still owns — never into the caller's
stack, which has already unwound. The caller reads the result only on the success
path. The producer's own `SipiImage` / probe lives on the worker thread's stack
and is freed only when the thread ends (never, if truly wedged): that is the
accepted, bounded, surfaced leak below.

### Deadline value

`EngineContext::decode_timeout_ms`, default `120000` (2 minutes). A legitimate
large-JP2 decode completes in seconds; a 2-minute wall clock trips only on a true
hang. It is an `EngineContext` field (not a layout-locked seam struct) so tests
inject a short value directly, and so it can later be wired to an operator knob.

Operator-tunable `SIPI_DECODE_TIMEOUT_MS` (clap → `sipi_init` overrides →
`EngineContext`) is a **recorded follow-up**, not part of this minimal fix: the
default is safe, and wiring the full clap → C-ABI → `EngineContext` path is a
separate surface. Recorded in the execution journal.

## Bounding and surfacing the leak (`wedged_threads`)

A wedged thread cannot be reclaimed short of a process restart, so the count of
leaked threads must be bounded and observable:

- **Bound.** At most `SIPI_NTHREADS` decodes run concurrently (the shell's
  two-lane admission pool), so leaks accumulate only at the rate of
  hang-triggering requests, one leaked thread per wedged decode.
- **Surface.** A `wedged_threads` gauge on the engine metrics singleton, bridged
  end to end per `CONVENTIONS.md` § Metrics: `Sipi::observability::Metrics`
  member + bump, the `SipiMetricsSnapshot` field with size/offset asserts, the
  `sipi_metrics_snapshot` populate site, the Rust `#[repr(C)]` mirror + layout
  test, the `GAUGES` row in `metrics.rs`, and the C++ `metrics_registry_test`
  inventory. Exported over OTLP and reported by `/health`.
- **Restart rule.** When `wedged_threads` reaches `nthreads - 1`, the pool is one
  lane away from total wedge and an operator (or the orchestrator's health gate)
  restarts the process. Documented in `docs/src/operation/health-endpoint.md`.

## Testing

- **Red on main by timeout.** Without the mechanism, invoking
  `SipiImage::read_shape` on the hang fixture never returns; a test that calls it
  raw hangs until the Bazel test timeout — red. This is the pre-fix state the
  deadline test replaces.
- **Deadline unit test** (`src/ffi/cpp/serve_image_test.cpp`): a `bare_engine()`
  with `decode_timeout_ms` set to a short value (e.g. 2000 ms), calling
  `build_image_response` over each hang fixture, asserts the call returns an
  error status within a bounded wall time (not the 2-minute production default)
  and that `wedged_threads` incremented. A positive control over a good JP2
  (`unit/lena512.jp2`) with a generous deadline returns a normal response and
  leaves `wedged_threads` unchanged. The hang fixtures stay out of every fuzz
  corpus (operational fact 5).

## Follow-through

- Removing the Phase 2 `continue-on-error` on the J2K libFuzzer leg
  (`.github/workflows/fuzz.yml`): the leg goes green once the deadline bounds the
  decode.
- Operator knob `SIPI_DECODE_TIMEOUT_MS` (follow-up; journal).
- Hot-path bench: a `std::thread` spawn per decode is O(µs) against an O(ms–s)
  decode, so no measurable regression is expected; a proper before/after needs a
  `-c opt` baseline and is deferred with the Phase 1 bench (journal).
