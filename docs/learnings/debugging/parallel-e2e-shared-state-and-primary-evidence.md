---
title: "A test failing after an unrelated change is usually the test, not the code — get primary evidence before escalating the diagnosis"
date: 2026-09-06
category: "debugging"
component: "testing"
module: "test/e2e harness (shared server) + the asan-ubsan CI leg"
problem_type: "test-isolation-and-misdiagnosis"
severity: "medium"
symptoms: "An e2e test (tiff_jpeg_compression_input) fails with `hyper UnexpectedEof during chunk size line` only under the parallel harness; later, every asan e2e fails with Connection refused; later, serve_image_test TIMEOUTs. Each looked like a production/codec/concurrency bug; none was."
root_cause: "Parallel e2e tests mutating on-disk state that a shared server relies on (a test deleting its response file before reading it; a second server started against the shared ./cache dir), plus an ASan thread-registry false positive — none of it a production defect."
tags: [e2e, test-isolation, parallel-tests, shared-state, asan, false-positive, primary-evidence, debugging, hyper, cache-dir, sipi, goose-chase]
related: []
issue: "DEV-7131"
---

# A test failing after an unrelated change is usually the test, not the code

This is the "goose chase" from the SIPI security-hardening wave-2 work (DEV-7131, PR #800):
several days of escalating diagnoses that were each disproven by one cheap primary-evidence
probe. The production code was correct throughout. Every real root cause was a **test-isolation
bug** or a **sanitizer false positive**. The transferable lesson is about *how the failures were
chased*, not the specific bugs.

## Problem

Three distinct failures appeared during the wave-2 branch, each after an unrelated change, each
initially attributed to the wrong layer:

1. **`//test/e2e:iiif_compliance` `tiff_jpeg_compression_input`** started failing with
   `hyper error: UnexpectedEof during chunk size line` — a response that looked truncated
   mid-stream. It surfaced right after S2-27 (a `BodyAbort` change that resets the connection on
   a post-commit encode failure), and only under the parallel e2e harness (a single decode via
   CLI/curl/one reqwest was always clean).
2. On the **`asan-ubsan` CI leg**, *every* e2e test failed with `Connection refused` — the server
   never came up.
3. After fixing (2), **`//src/ffi:serve_image_test` TIMEOUT**ed — and was first misattributed to
   the `tiff_roundtrip_fuzz` target failing in the same run.

## Investigation (the goose chase)

Failure 1 was chased through four hypotheses, each killed by a single primary-evidence probe:

| Hypothesis | Primary-evidence probe that killed it |
|---|---|
| libtiff 4.7.2 bump regression | Rebuilt the server pinned back to **4.7.1** — failed *identically*. `git show` confirmed the 4.7.1→4.7.2 diff was version-metadata only. Not the bump. |
| Deadlock in the JPEG-in-TIFF decode | `sample`d the threads: the stack showed **-O0 destructor churn**, not a lock wait. (Also: a fastbuild timeout ≠ a deadlock — retest under `-c opt`.) |
| Response truncation / missing EOI | Read the **raw socket bytes**: the response ended in a valid `ffd9\r\n0\r\n\r\n` — JPEG EOI **plus** the chunked-terminator. The bytes on the wire were complete. |
| Non-thread-safe global in libtiff/libjpeg (a real concurrency race S2-27 "surfaced") | This was the orchestrator's blocked conclusion. Disproven by the actual fix below. |

The breakthrough came from reading the **test**, not the code: the e2e harness runs tests in
parallel against one **shared** server, and `iiif_compliance.rs` called `remove_file(&dst)`
**before** `resp.bytes()`. A sibling test deleting the shared response file mid-read produced the
`UnexpectedEof`. It was never a codec, libtiff, or concurrency bug.

Failure 2 was an **ASan false positive**: `run_with_deadline` (the DEV-7080 decode watchdog)
created and joined a `std::thread` per decode. In the Rust shell's heavy thread-creation history,
ASan's thread registry reused a slot id and reported *"Joining already joined thread, aborting"* —
aborting the server on first use, so every e2e got `Connection refused`. The same false positive
was already documented and worked around for Kakadu's worker pool in `SipiIOJ2k.cpp`
(`num_threads = 0` under `__SANITIZE_ADDRESS__`).

Failure 3 was a **regression introduced by the fix for Failure 2**, and *also* first misattributed:
the CI summary was read as "tiff_roundtrip_fuzz failed", but the actual `test.log` showed
`tiff_roundtrip_fuzz PASSED` and `serve_image_test TIMEOUT`. The asan-inline guard compiled the
watchdog *thread* out; `serve_image_test`'s hang fixture (which feeds a non-returning producer and
expects a `nullopt` timeout) then hung forever because the deadline could never fire.

## Root Cause

- **T1 (`iiif_compliance`)**: test-lifecycle race — `remove_file` before `resp.bytes()` in a
  parallel harness sharing one server. A pre-existing latent bug that S2-27's connection-reset
  behaviour merely made *visible*.
- **T3 (`resource_limits`)**: the same class — two e2e tests started a *second* sipi process against
  the shared on-disk `./cache` dir (hardcoded in four `test/_test_data/config/*.lua`); the second
  process's `SipiCache` load treated the first's live files as orphans and **deleted** them, so the
  shared server's next cache hit 500'd and tanked unrelated tests.
- **Failure 2**: ASan thread-registry slot-id reuse — a false positive, not a real double-join.
- **Failure 3**: a build-config guard that compiles out a behaviour, without skipping the test that
  asserts that behaviour.

## Solution

- **T1** (`616dc953`): read `resp.bytes()` **before** `remove_file(&dst)` in
  `test/e2e/tests/iiif_compliance.rs`. Also removed the dead commented-out `inlock` mutex in
  `SipiIOJpeg.cpp` that the concurrency hypothesis had left behind.
- **T3** (`4f7ee64d`): give each concurrently-started server an isolated `tempfile::tempdir()` cache
  via `SIPI_CACHE_DIR` (mirroring `admission_control.rs`'s `start()`), instead of the shared `./cache`.
- **Failure 2** (`decode_guard.h`, folded into `14f7078d`): run the producer **inline under ASan**
  (`#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))`)
  — no worker thread, no join. ASan builds never ship, so the watchdog thread is test-only.
- **Failure 3** (`serve_image_test.cpp`): `GTEST_SKIP()` the `DecodeHangWatchdog.HitsDeadlineAndCountsWedgedThread`
  test under the same ASan macro — the behaviour it asserts is compiled out under ASan; it still runs
  on all three non-asan platforms.
- A genuine adjacent fix: `tiff_roundtrip_fuzz` gained a degenerate-image guard (skip encode when
  `getNx()/getNy()/getNc() == 0`).

## Prevention

**Debugging process (the headline):**

- **Get primary evidence before escalating a diagnosis.** A test that fails after an unrelated
  change is *usually the test*. Before blaming a dependency bump, a codec, or "a concurrency race the
  change surfaced," run the one cheap probe that would disprove it: rebuild at the *old* version;
  `sample`/`gdb` the threads instead of assuming a deadlock; read the *raw wire bytes* instead of
  assuming truncation. Each probe here took minutes and killed a hypothesis that had otherwise
  consumed hours.
- **A fastbuild timeout is not a deadlock.** Retest under `-c opt` before concluding; `-O0`
  destructor churn on large objects can blow a short test timeout.
- **Read the actual failing `test.log`, not the CI summary line.** Failure 3 was chased against the
  wrong target (`tiff_roundtrip_fuzz`) because the summary was trusted over the per-target log, which
  plainly said `serve_image_test TIMEOUT`.
- **When a fix trades one failure for another, suspect the fix.** The asan-inline guard cleared 19
  e2e failures and introduced one timeout — a build-config guard that compiles out a behaviour must
  also skip the tests that assert that behaviour.

**Test isolation (the recurring root cause):**

- **Parallel e2e tests that share a server must not mutate shared on-disk state.** Both real bugs
  were this. A test that writes/deletes a file the shared server serves, or starts a second server
  against a shared cache/tmp dir, is a systematic source of flaky `UnexpectedEof` / `500` signatures
  that masquerade as production bugs. Give each test its own `tempdir()` for any writable path
  (`SIPI_CACHE_DIR`, output files), and read a response body fully before touching its backing file.
- **Grep for shared literal paths in test fixtures.** Four `test/_test_data/config/*.lua` shared a
  literal `./cache`; that is a latent landmine the moment any test starts a second process.

**Sanitizer false positives (SIPI-specific but transferable):**

- A short-lived `std::thread` create+join in the C++ engine, when linked into the **Rust shell**,
  trips ASan's "Joining already joined thread" (slot-id reuse). Any new per-op thread needs the same
  `__SANITIZE_ADDRESS__` inline guard as the Kakadu pool and `run_with_deadline`, or the asan-ubsan
  leg goes red with a server-won't-start signature. Local ASan is broken on darwin, so this only
  shows on CI.

## References

- PR #800 (SIPI security hardening wave-2), branch `feature/dev-7131-…`.
- Fix commits: `616dc953` (T1 read-before-delete), `4f7ee64d` (T3 cache-dir isolation),
  `14f7078d` (shared decode guard incl. the asan-inline watchdog), `serve_image_test` GTEST_SKIP,
  `tiff_roundtrip_fuzz` degenerate guard.
- Related workaround precedent: `src/format_handlers/cpp/SipiIOJ2k.cpp` Kakadu `num_threads = 0`
  under `__SANITIZE_ADDRESS__`.
