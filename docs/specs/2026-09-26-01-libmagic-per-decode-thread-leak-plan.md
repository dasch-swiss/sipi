---
title: "fix(util): close the per-thread libmagic handle — per-request leak OOM-killed prod SIPI 9.1.0"
date: 2026-09-26
author: Ivan Subotic
status: implemented
repositories: []
---

# fix(util): close the per-thread libmagic handle

## Overview

On 2026-09-26 at 14:02 UTC, the kernel memory cgroup OOM-killed SIPI 9.1.0 on
`dasch-vre-prod-01`. Container `DSP_iiif_iiif.1.m8eubd…` was killed, and
`…q7f0u9…` started at 14:02:52. The cause is a **true per-request leak**, not
allocator retention and not a decode spike. Two changes combine to produce it,
and both landed between 8.0.0 and 9.1.0:

- `13c4d0e0` (DEV-7080): `run_with_deadline` (`src/ffi/cpp/decode_guard.h:55-64`)
  runs every shape probe and every decode on a **new `std::thread`**, joined on
  success.
- `1f79fa52`: `getFileMimetype` (`src/util/cpp/Parsing.cpp:148`) caches its
  loaded libmagic handle in a raw `thread_local magic_t` that is **never closed**.
  Its comment says "leaking it at process/thread exit is fine".

`SipiImage::read_shape` calls `getFileMimetype` (`src/image/cpp/SipiImage.cpp:396`)
inside the shape-probe deadline worker (`src/ffi/cpp/serve_image.cpp:534`).
`SipiImage::read` dispatches by extension and does not reach libmagic, so the
decode worker (`:808`) does not leak. Every IIIF image request therefore loads
one fresh libmagic handle on a thread that exits immediately, and leaks it.
Tokio blocking threads that call libmagic directly also leak one handle each
time they retire (`serve_image.cpp:117`). Their keep-alive is tokio's default in
production; the 365-day override is ASan-only (`src/server/rust/src/lib.rs:147-149`).

## Problem Statement / Motivation

Evidence, all from Grafana Cloud (vre-prod-01, read-only):

| Signal | 8.0.0 (09-10 → 09-23) | 9.1.0 (09-23 05:48 → 09-26 14:02) |
|---|---|---|
| `sipi_malloc_in_use_bytes` 6 h floor | 0.2–2 GB, returns to baseline | 0 → 1.4 → 3.8 → 7.9 → 11.2 → 13.3 GB, monotonic |
| `sipi_malloc_arena_bytes` (= RSS under mimalloc) at kill | n/a | 16.5 GB against the 16 GiB limit |
| `sipi_malloc_retained_bytes` at kill | n/a | ~1 GB, so this is not allocator retention |
| `sipi_decode_memory_used_bytes` | 0 between decodes | 0 at every scrape, so this is not a decode spike |
| `sipi_wedged_threads` | n/a | 0, so no detached decode threads |

- **Growth per request.** Across 6 h windows, growth is about 200–700 KB per
  admitted decode (`sipi_decode_memory_estimate_bytes_count`). One window reached
  3 MB, likely because the decode counter misses requests that run the shape probe
  without an admitted decode (inferred, not measured). Growth plateaus on low-traffic days (09-25 18:00 → 09-26 12:00 held at
  ~15 GB with <1k decodes/6 h). The day's peak traffic (9.5k decodes, 12–18 h)
  then pushed it over the limit.
- **Scale check.** The leaked unit is a libmagic `magic_set` (file 5.47:
  `file_ms_alloc`, `add_mlist`, `mlist_free_one` in `src/apprentice.c`). It
  consists of two `mlist`s (`MAGIC_SETS` = 2), their `magic_rxcomp` pointer arrays
  (one slot per magic entry, allocated at load), the regexes compiled during
  matching and cached in those arrays, and the output and level buffers. The
  embedded DB is referenced, not copied (`apprentice_buf`, `MAP_TYPE_USER`).
  `magic_close` frees all of it. A few hundred KB per handle is plausible (the
  entry count is inferred, not read) and matches the observed slope. Phase 1
  measures it.
- **Why tests missed it.** Under ASan, `run_with_deadline` runs the producer
  inline, with no worker thread (`decode_guard.h:42-51`). The sanitizer leg is the
  only place LSan runs, and it never exercises thread churn. Stage and dev carry
  almost no image traffic, so their in-use stays under 0.1 GB.
- **Stakes.** The same slope recurs until a fix is deployed. At the observed
  ~3–5 GB/day, conservatively expect the **next prod OOM around 09-29 to 09-30**.
  A high-traffic or cache-off day comes sooner.

This defeats the admission and memory-budget work of the past weeks by
construction. The decode budget (12 GiB of a 16 GiB envelope) governs decode
transients, but it cannot see steady-state RSS, and the leak consumed that
headroom silently.

## Proposed Solution

Make the per-thread handle close itself on thread exit. Hold it in a
`std::unique_ptr` with `magic_close` as the deleter. This is the repo's RAII
pattern for C handles (`docs/src/development/cpp-style-guide.md` §4.3, line 721):

```cpp
// Parsing.cpp
namespace {
using MagicHandle = std::unique_ptr<magic_set, decltype(&magic_close)>;
}// namespace

std::pair<std::string, std::string> getFileMimetype(const std::string &fpath)
{
  thread_local MagicHandle handle(nullptr, &magic_close);
  if (!handle) {
    MagicHandle new_handle(magic_open(MAGIC_MIME), &magic_close);
    if (!new_handle) { throw Error("magic_open() failed"); }
    void *bufs[] = { magic_mgc };
    size_t sizes[] = { magic_mgc_len };
    // magic_error's string lives in the handle; Error copies it before unwinding closes it.
    if (magic_load_buffers(new_handle.get(), bufs, sizes, 1) != 0) { throw Error(magic_error(new_handle.get())); }
    handle = std::move(new_handle);
  }
  std::string mimestr(magic_file(handle.get(), fpath.c_str()));
  return parseMimetype(mimestr);
}
```

- The `thread_local` destructor runs at thread exit, whether the thread was
  joined or not. Every thread kind is covered: deadline workers, tokio blocking
  threads when they retire, and the CLI main thread at process exit
  (`src/cli/cpp/commands/convert_service_file.cpp:146`).
- A thread that is reused keeps its handle, so the one-load-per-thread benefit
  `1f79fa52` was after is preserved.
- Short-lived shape-probe workers pay one DB load per thread. 8.0.0 paid the same
  per call, and that is the proven-flat baseline.
- A wedged, detached worker (`decode_guard.h:62`) never exits and keeps its handle.
  That is bounded by `sipi_wedged_threads` and already accounted for by DEV-7080.
- Replace the stale "leaking it at thread exit is fine" comment with the enduring
  constraint: callers may run on short-lived threads, so the handle is closed on
  thread exit.

## Technical Considerations

- **Scope is SIPI-only.** Ship as a 9.1.x patch release (`fix(util): …` drives the
  bump via release-please).
- **Regression guard.** A unit test that calls `getFileMimetype` from N short-lived
  `std::thread`s. It runs under the CI `sanitizer` job (unit + e2e under
  `--config=asan --config=ubsan`, LSan on, no libmagic entry in
  `.lsan_suppressions.txt`). The current code fails it with LSan leak reports, and
  the fix passes it. The test spawns its threads explicitly, so the ASan-inline
  `run_with_deadline` does not mask it.
- **Hot-path cost.** `Parsing.cpp` is not on the hot-path benchmark list. The fix
  restores 8.0.0's per-call open/load cost on deadline workers and keeps the cached
  handle on reused threads, so it needs no new microbench.
- **Cache hits leak too.** The shape probe runs before the cache check
  (`serve_image.cpp:534` vs `:702`), so every image request leaks a handle
  regardless of cache state.
- **Measurement source.** PID 1 in the image is tini, and mimalloc's `in_use`
  merges other threads' stats lazily. The replay therefore samples the
  container's cgroup memory (`docker stats --no-stream --format '{{.MemUsage}}'`),
  which is what the OOM killer acts on.
- **Completeness.** A read-only audit of everything reachable from
  `read_shape`/`read` found no other per-thread state that outlives thread exit:
  - Kakadu `kdu_thread_env` is destroyed on every exit path (`KduReadTeardown`).
  - The logger and timing `thread_local`s are C++ objects, destroyed at exit.
  - lcms2 contexts, Exiv2 XMP init and curl init are per call or per process.
  - mimalloc's own thread-exit behaviour was not checked against the pinned
    source. Phase 3 covers it empirically.
- **Thread-per-decode stays.** It is the DEV-7080 watchdog. Remaining per-request
  thread churn is known and not a leak:
  - one `std::thread` per shape probe and per decode;
  - a Kakadu pool of `num_threads` workers per J2K decode
    (`SipiIOJ2k.cpp:741-749`). It is sized from `kdu_get_num_processors()`,
    i.e. host cores rather than the cgroup quota (unverified: confirm in
    Kakadu's `kdu_arch.cpp`).

  If Phase 3 finds a residual slope, this churn is the next suspect.

## Implementation Phases

Work on a branch off `main`. The red test and the fix end up as one `fix(util)`
commit. The red-test state is only pushed to get a sanitizer run, and it is
folded into the fix commit before the PR.

#### Phase 1: Reproduce and size the leak

- [x] Create the branch `fix/libmagic-thread-handle-leak` off `main`.
- [x] Add `TEST(Parsing, GetFileMimetypeFromShortLivedThreadsDoesNotLeak)` to
  `src/util/cpp/parsing_test.cpp`:
  - It writes a minimal PNG (8-byte signature + IHDR chunk) into `$TEST_TMPDIR`.
    The leak is in the handle, not in what is sniffed, so this avoids an LFS
    fixture and a new `data` dep on `util_test`.
  - It starts 64 `std::thread`s concurrently, then joins all of them. Starting and
    joining one at a time is the slot-reuse shape that trips ASan's "Joining
    already joined thread" false positive.
  - Each thread body calls `getFileMimetype` inside `try`/`catch` and stores the
    result or the error text. An uncaught throw would `std::terminate` before
    LSan reports.
  - After joining, the test asserts all 64 results are `image/png`.
- [x] Push the branch and run the sanitizer leg: `gh workflow run ci.yml --ref fix/libmagic-thread-handle-leak`.
  CI triggers on `pull_request` and `workflow_dispatch` only, and local macOS ASan
  does not link.
- [x] Read the `sanitizer` job log (`gh run watch`, then `gh run view --log --job <sanitizer job id>`).
  `//src/util:util_test` fails with LSan reports whose allocation stacks sit under
  `magic_open` / `magic_load_buffers` / `magic_file`. Divide the reported total
  leaked bytes by 64 to get bytes per handle.
- [x] Build a replay harness under the scratchpad:
  - colima (`DOCKER_HOST` set to colima's socket), running the released `9.1.0`
    and `8.0.0` SIPI images;
  - a JP2 corpus mounted as the image root;
  - one identical `server --config` and env for all runs (no pre_flight Lua, the
    same cache setting);
  - a fixed load of 500 warm-up requests, then 10,000 image requests
    (tiles + `full/…`) at a fixed concurrency of 16;
  - `docker stats --no-stream` memory sampled every 30 s and after every 1,000
    requests.

  The cache state does not matter: the shape probe (`serve_image.cpp:534`) runs
  before the cache check (`:702`), so cache hits leak too.
- [x] Replay 9.1.0 and 8.0.0, each in a fresh container. Fit memory against
  cumulative requests after the warm-up. The 9.1.0 slope is ≥ 100 KB/request,
  which proves the harness reproduces prod. Record both slopes.

#### Phase 2: Fix

- [x] Replace the raw `thread_local magic_t` in `src/util/cpp/Parsing.cpp` with the
  `thread_local std::unique_ptr<magic_set, decltype(&magic_close)>` shown in
  Proposed Solution.
- [x] Rewrite the comment above it to state the enduring constraint: callers may
  run on short-lived threads, so the handle is closed on thread exit.
- [x] Push and rerun the sanitizer leg as in Phase 1. `//src/util:util_test` passes
  with no LSan report.
- [x] Run `just bazel-build`. It succeeds.
- [x] Run `just bazel-test`. It passes.
- [x] Run `just bazel-test-e2e`. It passes.

#### Phase 3: Prove the goal on the replay

- [x] Build the fixed image (`just bazel-docker-build-arm64`) and replay it in a
  fresh container with the identical Phase 1 load.
- [x] The fixed-image slope is ≤ 10 KB/request, and no more than 10 KB/request
  above the 8.0.0 slope. If it misses either bound, stop and report the three
  slopes. Do not ship this as the OOM fix; H5 decides the next step.

#### Phase 4: Ship

**Gate: H4** — resolve before starting this phase.

- [x] Squash the branch to one `fix(util): …` commit carrying the test and the fix,
  with the DEV reference.
- [x] Run `just bazel-rustfmt-check`. It passes.
- [x] Run `just bazel-clippy-check`. It passes.
- [x] Force-push the branch with the lease pinned to the last-pushed SHA.
- [x] Open the PR using `.github/PULL_REQUEST_TEMPLATE.md`.

## Human Actions

| Id | Who | When | Action | Why an agent cannot |
|---|---|---|---|---|
| H1 | Ivan | After merge | Cut the 9.1.x release (release-please PR), deploy it to prod via ops-deploy + dsp-api pin bump, then confirm prod `sipi_malloc_in_use_bytes` 6 h floor stays under 2 GB for 72 h of normal traffic | Deploys are operator-only; the 72 h observation outlives the run |
| H2 | Ivan | Now, independent of this plan | Review, merge, and deploy ops-deploy PR #1444, the fix for the disabled IIIF cache. Cause: ops-deploy `96b896ac` (#1426) added `cap_drop: [ALL]`, so the in-container root lost `CAP_DAC_OVERRIDE`. The Ansible-created `DSP_IIIF_DATA_CACHE_DIR` has no `owner`, so root cannot write it; `SipiCache` then logs `Cannot create cache directory` and runs without a cache. This happens on every start since stage 09-14 and prod 09-16. The fix creates the cache and tmp directories owned by root. Before deploying, confirm read-only with `stat -c '%U:%G %a'` on the host dir | Deploys are operator-only |
| H3 | Ivan | Until H1 lands | Decide on an interim guard. For example, watch `sipi_malloc_arena_bytes{deployment_environment_name="vre-prod-01"}` against the 16 GiB limit and restart deliberately before ~14 GB | Prod operation decision |
| H4 | Ivan | After Phase 3 passes | Approve opening the PR, and provide or approve the DEV issue it references | Standing practice: PR creation needs explicit approval |
| H5 | Ivan | Only if Phase 3 misses its bound | Decide the remedy for the residual slope (next suspect: per-request thread churn in `run_with_deadline` / Kakadu pools) | Design decision on the DEV-7080 watchdog |

## Alternative Approaches Considered

- **One process-wide handle behind a mutex.** No per-thread loads, but it
  serializes `magic_file` (which reads file bytes) across up to 100 in-flight
  requests. Rejected: it adds contention for no measured need.
- **Remove thread-per-decode (a persistent watchdog worker).** This removes all
  per-request thread churn, but it is a larger redesign of DEV-7080 and is not
  needed to stop this leak. It is kept as the Phase 3 contingency, driven by data.
- **Revert `1f79fa52`'s caching.** This works, but it also drops the per-thread
  caching the docroot path benefits from. The RAII holder keeps both.

## Acceptance Criteria

- [x] The Phase 1 unit test fails under LSan without the fix and passes with it.
- [x] On the Phase 1 replay, 9.1.0 climbs ≥ 100 KB/request and the fixed image
  stays ≤ 10 KB/request, within 10 KB/request of 8.0.0.
- [x] `just bazel-build`, `just bazel-test`, `just bazel-test-e2e`,
  `just bazel-rustfmt-check`, and `just bazel-clippy-check` pass. No Rust change
  is expected, but the lint gates are mandatory before any commit.

## Success Metrics

- Baseline, prod 8.0.0: in-use 6 h floor 0.2–2 GB over 09-17 → 09-23.
- Regression, prod 9.1.0: floor 0 → 13 GB in 72 h, ~200–700 KB per decode.
- Replay gate: 9.1.0 ≥ 100 KB/request (harness validity); fixed ≤ 10 KB/request
  and within 10 KB/request of 8.0.0.
- Post-deploy (H1): prod 6 h floor under 2 GB for 72 h.

## Dependencies & Risks

- **Residual slope.** Thread-per-decode may carry its own allocator residue. The
  Phase 3 gate catches this before anything ships as "the fix".
- **The cache-off prod state (H2)** is independent of the leak. 8.0.0 ran
  cache-off for 7 days and stayed flat, and cache hits leak a handle anyway. It
  still costs a full decode per tile, which is a performance problem in its own
  right. Traefik in-flight went 20 → 100 on 09-23
  (ops-deploy `9f19e0fb`), which also raises peak concurrency.
- **Structural gap, noted and not in scope.** The admission envelope guards decode
  transients only. No mechanism reacts to steady-state RSS approaching the cgroup
  limit, so any future leak repeats this silently until the kernel kills the
  process.

## References

- `src/ffi/cpp/decode_guard.h:39-66`: `run_with_deadline`, thread per call, inline under ASan
- `src/ffi/cpp/serve_image.cpp:534`, `:808`: shape probe (leaks) and decode (does not reach libmagic) under the deadline
- `src/image/cpp/SipiImage.cpp:396`: `read_shape` → `getFileMimetype`
- `src/util/cpp/Parsing.cpp:148-174`: the leaking `thread_local magic_t`
- `src/server/rust/src/malloc_stats.rs`, `src/cli/rust/src/main.rs:180-206`: gauge semantics (mimalloc)
- `src/server/rust/src/lib.rs:138-149`: 365-day blocking keep-alive is ASan-only
- Commits: `13c4d0e0` (DEV-7080 seam deadline), `1f79fa52` (libmagic thread_local cache)
- ops-deploy `9f19e0fb`: Traefik in-flight 20 → 100
