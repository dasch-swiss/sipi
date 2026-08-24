---
title: "SIPI cost-based two-lane admission control"
type: feat
date: 2026-08-11
author: "Ivan Subotic"
status: draft
repositories:
  - name: sipi
    path: /Users/subotic/_github.com/dasch-swiss/sipi
  - name: ops-deploy
    path: /Users/subotic/_github.com/dasch-swiss/ops-deploy
  - name: dsp-api
    path: /Users/subotic/_github.com/dasch-swiss/dsp-api
prd:      # none — this plan is the source of truth (originated from a prod incident)
linear:   # none assigned
---

# SIPI cost-based two-lane admission control

## Overview

Replace SIPI's removed per-IP rate limiter with a **priority two-lane admission
pool** so that expensive full-image decodes (overwhelmingly distributed crawler
bots) can neither OOM-kill the server nor starve legitimate IIIF tile traffic.
Tiles (the viewer path) get a guaranteed floor and burst into idle capacity;
full-size downloads are hard-capped in both threads and memory and shed with 503
when their share is exhausted. Everything derives from two hard caps — RAM and
CPU — plus two tile ratios and the two-tier `basic`/`advanced` mode. Ship in
`basic` (advanced tier observe-only on Grafana, no ops-deploy change), then flip
to `advanced`.

## Problem Statement / Motivation

Since 2026-08-10 a distributed crawler swarm (Amazonbot, Bytespider, Meta, plus
spoofed-Chrome UAs across many IPs) pulling large `/full/` downscales repeatedly
OOM-killed SIPI on `vre-prod-01`. Evidence (Grafana Cloud `grafanacloud-prom`/`-logs`):

- **Cost is bimodal.** 13 h decode-estimate histogram (~19,600 decodes): 68 % < 10 MB
  (tiles), **27 % (~5,300) 100–500 MB each**. Median 6.9 MB, p99 509 MB.
- **Concurrency × cost, uncapped.** `nthreads=16`, cost-blind → up to 16 × 0.1–0.5 GB
  of decode buffers → RSS past the envelope → kernel OOM-kill (sawtooth to ~3.7 GiB
  against the old ~4 GiB envelope).
- **Both safety valves were observe-only** (`rate_limit_mode`/`decode_memory_mode` =
  `monitor`): 19,460 admits, **0 rejections**.
- **Per-IP is the wrong axis** — a distributed swarm spreads under every per-client
  budget. Removed in PR1.

**Resource model:** At the time of the incident SIPI was not cgroup-capped on
`vre-prod-01` — it saw the whole VM RAM. (SIPI v5 now auto-detects the cgroup limit,
and ops-deploy sets one via `DSP_IIIF_CONTAINER_MEMORY_LIMIT`; see "One total-memory
knob".) It self-limits via `SIPI_NTHREADS` + the decode-memory envelope. Raising
VM RAM is explicitly not the goal.

## Proposed Solution

A two-lane pool derived from the (RAM, CPU) envelope, with **tiles as floors** and
**full downloads hard-capped**:

- **Threads.** `tile_min = round(nthreads × tiles_thread_ratio)` (0.5 → 8) guaranteed
  to tiles; `full_max = nthreads − tile_min` (→ 8) hard-caps concurrent full decodes.
  Tiles burst above their floor into any idle capacity up to `nthreads`; full stays
  capped even when tiles are idle.
- **Memory.** `full_mem = envelope × (1 − tiles_memory_ratio)` (0.25 → 75 % of the
  envelope) hard-caps full-lane accounted decode bytes. Tiles bypass the budget. The
  reserve `envelope × tiles_memory_ratio` (25 %) is never charged to full and houses
  tile usage + the non-decode floor (base heap, mimalloc retention, HTTP/encode,
  cache). Invariant: `reserve ≥ observed floor`.
- **Mode.** `admission_mode` = `basic | advanced` (no `off`, default `basic`): the
  two tiers, not observe-vs-act. `basic` enforces the CPU/thread cap only and leaves
  the advanced tier observe-only (shadow-counts what it would shed); `advanced` also
  enforces the memory + two-lane caps + 503/413. An unrecognized value falls back to
  `basic` (no startup error).
- **Four envelope knobs** — SIPI env vars (ops-deploy exposes them as `DSP_IIIF_*`
  Ansible variables that render into these; also settable as CLI flags / Rust TOML
  config keys — the two-lane pool config is shell-owned, **not** on the Lua surface),
  everything else derived: `SIPI_MEMORY_LIMIT`, `SIPI_NTHREADS`,
  `SIPI_TILES_THREAD_RATIO` (0.5), `SIPI_TILES_MEMORY_RATIO` (0.25) — plus the
  `SIPI_ADMISSION_MODE` mode. Defaults live in the binary → observable on Grafana with
  no ops-deploy change.

## Technical Considerations

### Module shape: a colocated `src/throttling/` polyglot (dune review DUNE-002, extended)
The Throttling umbrella becomes structural: a component-first, language-second
polyglot package per the ADR-0021 pattern —

```
src/throttling/
├── cpp/    cc_library memory_budget — SipiMemoryBudget.{h,cpp} + SipiPeakMemory.h,
│           carved out of //src:engine (engine-side, post-cache; ADR-0008 gate)
└── rust/   rust_library admission — the two-lane pool (shell-side, pre-dispatch)
```

**The Rust side** (`//src/throttling/rust:admission`) is FFI-free: deps = tokio +
`//src/iiifparser/rust:iiif_parser` only — no `//src/ffi`, no C++ engine,
sanitizer-eligible, and its concurrency tests run without linking Kakadu. It
exposes one concrete type (no trait, no port): `Admission`, built once from plain
config values (`nthreads`, both ratios, mode, `max_waiting`, `queue_timeout`,
threshold), owning both semaphores **and** the per-lane wait/shed counters
(today's `WAITING`/`LOAD_SHED_TOTAL`/`QUEUE_TIMEOUT_TOTAL` module statics in
`routes.rs` move inside it — single writer). Interface: `classify(...) -> Lane`
(from domain `IiifParams`/`RequestKind` + the threshold), `acquire(Lane) -> Permit`
(RAII, encapsulating full-lane-first ordering), and a counters snapshot for the
OTLP bridge. `AppState` owns the instance; `routes.rs` keeps only call sites;
`metrics.rs` reads the snapshot.

**The C++ side** (`//src/throttling/cpp:memory_budget`) is a move-plus-carve, not
a rewrite: the class is already self-contained; carving it out of `//src:engine`
also lets its unit test link the narrow target instead of `//src:sipi_lib`
(ADR-0003 direction, same as the iiifparser/formats carves).

**~~Deliberately not colocated: the output size guard.~~ SUPERSEDED (2026-08-18,
`aa007c7d`): the output size guard was removed entirely.** The original plan argued
the ~10-line inline `max_pixel_limit` check at the gate site should stay in place
(not colocated into the module). The follow-on config audit found it an always-`0`
guard no deployment enabled — oversized upscales are already covered by the memory
budget's peak estimate, which sizes the scaled *output* buffer — so `max_pixel_limit`
and its `image_too_large_total` metric were deleted, along with the ARCH-MAP pointer.
See the 2026-08-18 follow-on section. The throttling package's findable index is now
just the `cpp/` budget + `rust/` admission pair; there is no third inline guard.

**Naming:** the package carries the umbrella (`throttling`, structural home for
the sub-policies); the crate and type carry the mechanism (`admission`) — the
`//src/iiifparser/rust:iiif_parser` labeling pattern. Unlike ADR-0021 this is not
a strangler pairing (both sides are permanent, different pipeline stages); the
colocation payoff is one directory answering "what sheds load?".

### Acquisition order (correctness-critical)
A full request acquires the **full-lane permit FIRST, then the global permit** — never
the reverse. Global-first would let `N` queued fulls hold every global permit while
blocked on the full-lane semaphore, destroying the tile floor. Full-lane-first bounds
the number of fulls that can enter the global FIFO ahead of tiles to `≤ full_max`.

### Tile priority is bounded, not absolute — state it honestly
`tokio::sync::Semaphore` is FIFO; a full already parked in the global queue does **not**
yield to a tile that arrives later. The full-lane-first ordering (above) is the actual
mechanism that preserves tile headroom (`≥ tile_min` global permits always free of
fulls). "Full yields to bursting tiles" is an approximation via this bound — the plan
states it as an explicit design choice, not automatic preemption.

### Per-lane wait accounting
`WAITING`/`max_waiting` is currently one process-wide counter (`routes.rs:202,261`);
a full burst would fill it and shed subsequent **tiles**. Tiles must be **exempt from
the full-request queue-depth shed**: a tile sheds only when no global permit is
genuinely available, never because the queue is full of fulls. Track waiting/shed
per lane.

### Memory saturation is an immediate 503, not a wait
`SipiMemoryBudget::try_acquire` is a one-shot lock-free CAS (`SipiMemoryBudget.cpp:28-55`)
that never blocks, fired deep in the FFI after the thread + both permits are already
held. Full-lane memory exhaustion → **immediate 503** (no "wait up to queue_timeout");
a blocking wait here would pin scarce permits and add a second starvation vector.

### Permanently-unservable requests get a distinct signal
If a single request's `estimated` alone exceeds `full_mem`, `try_acquire` fails on the
first CAS regardless of load (`SipiMemoryBudget.cpp:42-46`) — it will always 503, so a
`Retry-After` is misleading. Return **413 Payload Too Large** (no `Retry-After`) + a
distinct metric/log, separate from "budget currently exhausted" (503 + `Retry-After`).

### Two classifiers, one intent — define both and measure disagreement
Lane routing needs a decision in the Rust shell *before* dispatch (native dims unknown
there), while the memory budget classifies precisely in the engine from
`estimate_peak_memory`. **Placement (ADR-0021):** the shell classifier consumes the
domain `IiifParams` returned by `iiif_parser::parse_request`
(`src/iiifparser/rust/request.rs:92`) but lives in the `admission` crate (see
Module shape) — the `iiif_parser` crate is pure FFI-free URL grammar and must not
grow admission policy or config knobs. These are two classifiers over two inputs
and will disagree:
- **shell-tile / engine-full** rides the tile thread floor but is still memory-gated
  (safe for RAM; can burn tile CPU headroom — and is uncapped on threads in `basic`).
- **shell-full / engine-tile** wastes a full-lane thread slot on a cheap request.
Define a single `large_decode_threshold_bytes` **once, in the shell config** (code
default, e.g. 32 MiB; tunable later) and **pass it over the FFI seam at init** as a
`SipiServerConfig` field — the engine never carries its own copy, so the two sides
cannot drift when it is tuned (dune review, DUNE-003; the Commit 2 ABI relock is
happening anyway, one more field is nearly free). Derive the shell's pixel-count
heuristic from it via a conservative bytes-per-pixel proxy, so the two classifiers
track. Export **both** thresholds and a **disagreement counter** so residual
heuristic drift is observable.

### One total-memory knob: `SIPI_MEMORY_LIMIT` (decided with maintainer)
There is exactly **one** memory knob. `SIPI_MEMORY_LIMIT` (ops-deploy Ansible var
`DSP_IIIF_MEMORY_LIMIT`) is SIPI's decode-memory envelope; **`0` → auto-detect the
container's cgroup memory limit** (`detect_available_memory()` reads cgroup v2
`/sys/fs/cgroup/memory.max`, then cgroup v1, then `/proc/meminfo`).
The redundant `max_decode_memory` / `SIPI_MAX_DECODE_MEMORY` is **removed**;
SIPI reads the envelope
directly (no separate decode-budget cap, no legacy "75 % of RAM" auto path).
Mechanically this is an ABI **rename** of the existing `max_decode_memory` string field
to `memory_limit` (same slot), so it is not an add+remove churn.
`full_mem = envelope × (1 − tiles_memory_ratio)`; the tile reserve
`envelope × tiles_memory_ratio` (25 %) is the headroom that houses the non-decode floor.
Re-verify `reserve ≥ observed floor` in `basic` before flipping to `advanced`, against
both an explicit limit and the `0`→auto-detected-cgroup path. **Wiring (done):** ops-deploy renders
`DSP_IIIF_MEMORY_LIMIT` into `SIPI_MEMORY_LIMIT` and sets the container cgroup cap via the
separate `DSP_IIIF_CONTAINER_MEMORY_LIMIT` (`deploy.resources.limits.memory`). With a cgroup
cap in place, `0`→auto sizes the envelope to that cgroup cap, not the VM RAM.

### Single source of truth for `admission_mode`
The mode is needed by both the shell (thread lanes) and the engine (memory budget), and
is single-sourced across the FFI seam — not two independently-read config surfaces. Final
direction (maintainer, 2026-08-16, decision #4): the **engine resolves** the mode and the
**shell reads it back** over the `sipi_admission_mode` getter, so both sides agree.
An unrecognized/legacy value (e.g. a stale `"off"`/`"monitor"`/`"enforce"` from an old
template) **falls back to `basic`** — **NOT** a startup error. (Superseded 2026-08-18,
`e443a26f`: the original plan called for fail-loud; it was changed to silent fallback for
consistency with the rest of config and safe migration — a stale value degrades to `basic`
rather than crashing startup.)

### Lane policy per request kind (all paths through `acquire_or_shed`)
- **IIIF image** — classified tile/full (the only classified kind).
- **info.json / knora.json** — cheap metadata reads, no decode → **tile lane**.
- **/file download** — raw byte stream, no decode, so no memory budget; **tile lane** for
  admission. Note it holds a thread for the (possibly multi-GB) transfer — transport-bound,
  not memory-bound; accepted.
- **Lua routes** — separate `acquire_or_shed` (`routes.rs:1049`), not routed by
  `parse_request`; take a **global permit only** (no thread lane). Any decode they trigger
  is still charged to the full-lane memory budget engine-side. Documented, accepted.
- **Redirect** — engine-free, no lane (handled before the pool).
- **HEAD / passthrough / cache-hit** — already bypass the memory budget (engine returns
  early, `serve_image.cpp:573-610`). Thread-lane classification is pre-dispatch, so a
  "full" cache-hit briefly holds a full-lane permit; accepted (throttle by class, released
  fast). Documented UX tradeoff.

### Degenerate configuration
Validate ratios ∈ (0,1) at startup; `tile_min` and `full_max` clamp to ≥ 1 for
`nthreads ≥ 2`; for `nthreads < 2` disable lane separation (single global permit; memory
mode still applies). Fail loud on out-of-range ratios.

### Architecture constraints (ADR-0020 / ADR-0021 / ARCH-MAP, landed after this plan's first draft)
- **The differential parity gate no longer exists** (ADR-0020, oracle removal). The
  regression net is subject-only: **e2e + approval + proptest + unit**. Enforce-mode
  503/413 behavior is covered by a dedicated e2e suite; there is no oracle to diverge
  from and no `differential-coverage-check` gate to keep green.
- **The Rust IIIF parser is the carved `iiif_parser` crate** (ADR-0021,
  `//src/iiifparser/rust:iiif_parser`) emitting domain types (`IiifParams`,
  `RegionKind`/`SizeKind`/…, `ParsedRequest`/`RequestKind`); `server-rs` owns the
  domain→seam flattening (`From<iiif_parser::IiifParams> for SipiIiifParams`,
  `src/server-rs/src/ffi.rs:217`, exhaustive matches — never `as` casts). The crate is
  FFI-free and config-free: the tile/full classifier and `large_decode_threshold_bytes`
  belong in the new `admission` crate (see Module shape), never in the parser crate.
- **ARCH-MAP boundary rules that bind this work:** `memory-budget` writes no metrics
  itself — the acquire site in `serve_image.cpp` re-publishes gauges via the guard's
  `on_release` callback; a new engine scalar counter reaches production **only** if it
  is also read into `SipiMetricsSnapshot` and mapped in `metrics.rs`
  (`metrics_registry_test.cpp` pins the full inventory — currently 20 bridged / 13
  engine-internal — and must be updated for every new counter); the shell's Semaphore
  pool and the engine's post-cache Throttling gate are deliberately two separate
  admission layers. Use `Throttling` vocabulary per `UBIQUITOUS_LANGUAGE.md`
  ("backpressure" is a banned alias).

### Cross-cutting constraints (from institutional learnings)
- **FFI ABI is double-locked** — `static_assert` offsets in `src/ffi/sipi_ffi.h` **and** a
  mirrored `layout` test in `src/server-rs/src/config.rs`. Any field add/remove recomputes
  both (PR1 relocked `SipiServerConfig` 296→256, `SipiMetricsSnapshot` 200→160).
- **`sipi_malloc_arena/retained_bytes` are misleading** (mimalloc `committed`, not RSS) —
  do not use them as an OOM/RSS proxy in acceptance criteria; remap is the lane-metrics commit.
- **New OTLP counters:** verify temporality; if session/delta-scoped use `max_over_time`,
  not `increase()` (documented PromQL trap).
- **No C++ OTel SDK** — any engine-side spans must be Rust-minted from FFI phase timings.
- **Dashboard lives in dsp-api**, driven by `sipi_*` OTLP metrics (now on prod).

## Session progress & handoff (2026-08-16, updated)

**State: the entire sipi PR is code-complete and green (Commits 1–4 + e2e); only
the ops-deploy and dsp-api PRs remain.** Branch `worktree-two-lane-implementation`
(worktree at `.claude/worktrees/two-lane-implementation`), off `main` @ `a0c56d1f`.

**The sipi PR is a clean 7-commit stack, pushed to `origin` (force-pushed after a
guideline cleanup — see below). Not yet opened as a PR.** From `a0c56d1f`:
- `e1f03533` `refactor(throttling)` — Commit 1 (carve `src/throttling/cpp`).
- `ce02be22` `feat(throttling)` — Commit 2 (engine full-lane budget + admission mode).
- `01b38777` `feat(throttling)` — Commit 3 (admission crate + shell integration; now
  also carries its own lint-gate coverage and the refreshed `sipi-server-help`
  snapshot for the renamed flags).
- `7134eea7` `feat(observability)` — runtime admission metrics: bridge the engine 413
  counters through `SipiMetricsSnapshot` (relock 160→176, registry 20→22),
  per-partition waiting/shed + `full_in_use` + `full_shadow_rejected`, and the
  shell-only `classifier_disagreement` counter (compares the shell verdict vs
  `SipiServeTimings.decode_estimate_bytes`; no ABI/engine change).
- `9d7f69ff` `fix(observability)` — remap `sipi_malloc_arena_bytes`/`retained_bytes`
  to true RSS via `mi_process_info current_rss` (Linux/mimalloc-only, CI-verified).
- `892fa6fe` `docs(observability)` — admission + memory-budget metric docs + temporality note.
- `0374bdb0` `test(throttling)` — `admission_control.rs` e2e (413 + tile bypass +
  in-budget + monitor, each on its own empty cache dir); deletes the stale
  `memory_budget.rs` (Commit 2's rename left it referencing removed flags).

**Guideline cleanup (commit-conventions):** the review/gap fixes made during the
session (lint-gate coverage, the CLI-help snapshot regen, and the stale
count/comment corrections a 4-reviewer adversarial pass surfaced) were first
committed standalone, then folded via `git rebase --autosquash` into their
originating commits per "in-branch fixes don't land as their own commits."
Content is byte-identical to the pre-fold tree; the fold was verified with
`git diff` before force-pushing (lease pinned to the prior remote SHA).

All gates green locally on the rewritten tip: `bazel-rustfmt-check`,
`bazel-clippy-check`, `bazel-test-unit` (27), both binaries build, `commit-lint`,
and the `cli` + `admission_control` e2e pass. `bazel-coverage` not run locally
(CI). The mimalloc shim + `#[cfg(feature="mimalloc")]` block are CI-verified only
(macOS links no mimalloc; the C shim is `target_compatible_with` linux).

**Known per-commit caveat:** `ce02be22` renamed the CLI flags but the help-snapshot
fix lands in `01b38777` (where `--tiles-thread-ratio` is also added, so the full
snapshot is correct there). So `ce02be22`'s `cli` e2e snapshot is transiently stale
in isolation — pre-existing (e2e was deferred per-commit), tip is green, and CI
gates the tip. Regenerating an intermediate snapshot at `ce02be22` would close it if
strict per-commit e2e-greenness is wanted.

**Naming/deviation notes for review:**
- Metric names follow the maintainer's `admission` decision. Two small deviations
  from the Commit-4 checklist's literal names: the full-cap gauge stays
  `sipi_admission_full_max_threads` (already shipped in Commit 3) rather than a new
  `_max`; and the monitor would-be-reject counter is `full_shadow_rejected` with
  real enforce sheds in `full_shed` (the crate separates them) rather than one
  conflated `full_rejected_total`.
- Transient 503 is **not** an e2e test (see `admission_control.rs` module doc): a
  live-server 503 needs two decodes holding the budget simultaneously, an
  unavoidable timing race. It is deterministically unit-covered.

**Original handoff (pre-session), retained for context:**

sipi Commits 1–3 landed and green; Commit 4 + e2e + the ops-deploy and
dsp-api PRs remained.

Landed commits (each self-contained, all gates green — `bazel-rustfmt-check`,
`bazel-clippy-check`, `bazel-test-unit` (27 tests), both binaries build,
`commit-lint`):
- `e1f03533` `refactor(throttling)` — Commit 1 (move-only carve → `src/throttling/cpp`).
- `ce02be22` `feat(throttling)` — Commit 2 (engine full-lane budget + `AdmissionMode` + 413/503; `SipiServerConfig` relocked 256→280 on both sides).
- `01b38777` `feat(throttling)` — Commit 3 (admission crate + shell integration + read-back seam getters + `sipi_admission_*` metrics + fingerprint + ADR-0022/glossary/ARCH-MAP/docs).

**Maintainer decisions this session (binding for the rest of the work):**
1. **Vocabulary = `admission`, no "lane" anywhere.** Public API `Admission` (pool)
   + `AdmissionKind { Tile, Full }`. All metrics are `sipi_admission_*` (the old
   `sipi_pool_*` were renamed — no pool/admission mixing). Glossary uses "Tile
   partition / Full partition"; the "admission control" alias ban was lifted.
   → **Commit 4 must use `sipi_admission_full_in_use/_max/_rejected_total`,
   `sipi_admission_{tile,full}_waiting`/`_shed`, `sipi_admission_classifier_disagreement_total`
   — NOT the `sipi_*_lane_*` names written in the Commit 4 checklist below.**
2. **Commit scope `throttling`** for commits 1–3; **Commit 4 = `observability`**
   (lane metrics + allocator remap) per the concern-not-directory rule.
3. **Engine enum renamed `MemoryBudgetMode` → `AdmissionMode`** (`parse_admission_mode`).
4. **Single-source direction = engine-authority + shell read-back.** The engine
   resolves `admission_mode`/`tiles_memory_ratio`/`large_decode_threshold_bytes`/
   resolved `memory_limit`; the shell reads them back over four seam getters
   (`sipi_admission_mode`, `sipi_tiles_memory_ratio`,
   `sipi_large_decode_threshold_bytes`, `sipi_memory_limit_bytes`) so both sides
   agree. `tiles_thread_ratio` is a shell-only serve arg. `large_decode_threshold_bytes`
   default (32 MiB) is single-defined in `config.rs` (`DEFAULT_LARGE_DECODE_THRESHOLD_BYTES`),
   always sent over the seam.
5. **Monitor is observe-only for the thread cap** (shipping the default binary
   changes no behavior); the crate shadow-counts would-be full-cap rejections
   (`full_shadow_rejected_total` in `AdmissionSnapshot`).

**Deferred (still to do):**
- ~~**Commit 4** (`observability`)~~ — ✅ DONE this session (split into 4 commits;
  the classifier-disagreement counter is shell-only, so the feared per-request
  engine→shell signal was unnecessary — the shell already gets the engine estimate
  back via `SipiServeTimings`).
- ~~**e2e admission suite**~~ — ✅ DONE (`admission_control.rs`; 413 e2e + transient
  503 unit-covered).
- **ops-deploy PR** and **dsp-api dashboard PR** (separate repos; dashboard must
  use the `sipi_admission_*` names, not `sipi_*_lane_*`). **Not started.** Ship
  after the sipi PR merges and a binary carrying the new knobs exists.

**To resume:** the sipi PR is open as [sipi#783](https://github.com/dasch-swiss/sipi/pull/783)
(`feat(throttling): cost-based two-lane admission control`, branch
`worktree-two-lane-implementation`, commit-by-commit review). ops-deploy is done on
branch `sipi-v5-config-alignment` (see the 2026-08-18 subsection). Remaining: merge #783
→ ship the binary → apply ops-deploy, then the dsp-api dashboard PR per its checklist
below. Dashboard panels must use the shipped metric names:
`sipi_admission_full_in_use`, `sipi_admission_{tile,full}_waiting`/`_shed`,
`sipi_admission_full_shadow_rejected`, `sipi_admission_classifier_disagreement`,
`sipi_decode_memory_too_large_total`, and `sipi_malloc_arena_bytes` (now true RSS).

## Session progress & handoff (2026-08-18) — follow-on refactors

Two more commits landed on the sipi PR after the 2026-08-16 handoff above, taking
the stack from 7 to **9 commits**. Both are `refactor`; neither changes admission
*logic*, but both reverse or extend design decisions written earlier in this plan,
so they are recorded here (and the superseded plan text is flagged, not silently
left stale).

### `e443a26f` `refactor(throttling)` — rename `admission_mode` + take pool config off Lua

- **`admission_mode` renamed `monitor`/`enforce` → `basic`/`advanced`.** The values
  now name the two *tiers*, not observe-vs-act: `basic` (default) enforces the
  CPU/thread concurrency cap only and leaves the advanced tier observe-only
  (shadow-counting what it would shed); `advanced` also enforces the memory budget +
  two-lane load-shedding. Renamed across the Rust pool, the C++ budget, the FFI seam,
  config, metrics (`sipi.admission.mode` `0=basic`/`1=advanced`), logs, docs, tests.
  **Supersedes** every `monitor`/`enforce` mention in the sections above (Overview,
  Proposed Solution → Mode, Technical Considerations, Acceptance Criteria, ops-deploy
  flip). The ops-deploy flip is now `DSP_IIIF_ADMISSION_MODE=advanced`.
- **Unknown mode value → silent fallback to `basic`, not a startup error.**
  **Reverses** the "Single source of truth for `admission_mode`" decision above
  ("a startup error (fail loud), never a silent default") and the matching
  Acceptance Criterion ("a mismatched/legacy value fails at startup"). Rationale:
  consistency with the rest of config (unknown keys are ignored) and safe migration
  — a stale `monitor`/`enforce`/`off` in a live deploy degrades to `basic` instead
  of crashing startup.
- **Pool config is shell-owned, off the Lua surface:**
  - *Dead knobs removed outright* (fed the removed C++ mongoose server, ADR-0020,
    zero live callers): `max_waiting_connections`, the Lua `queue_timeout`, and the
    orphaned Lua `nthreads` key plus its whole C++/FFI chain (`sipi_nthreads`,
    `getNThreads`, `engine_context.nthreads`, `config.n_threads`). Worker-pool sizing
    and the wait queue are CLI/env only (`--nthreads`/`SIPI_NTHREADS`,
    `--max-waiting`/`SIPI_MAX_WAITING`, `--queue-timeout`/`SIPI_QUEUE_TIMEOUT`).
  - *Live two-lane knobs off the Lua config* but still settable via TOML config +
    CLI/env: `admission_mode`, `memory_limit`, `tiles_memory_ratio`. `SipiConf`
    defaults (`basic`/`0`/`0.25`) + the override path preserve behavior; a stray Lua
    key is silently ignored.

### `aa007c7d` `refactor(ffi)` — remove the dead config surface + apply the engine log level (net −607 lines)

A follow-on audit of every `config/sipi.config.lua` key (`SipiConf` → `lua_config.cpp`
→ serve path, plus the Rust CLI/env/TOML path) removed the remaining dead surface and
fixed the one real bug it surfaced.

- **`max_pixel_limit` + the `image_too_large_total` metric removed.**
  **Reverses** the plan's "Deliberately not colocated: the output size guard"
  subsection (which argued the ~10-line inline guard at `serve_image.cpp:529-538`
  stays). It was an always-`0` guard no deployment enabled; oversized upscales are
  already covered by the memory budget's peak estimate (which sizes the scaled
  *output* buffer). The ARCH-MAP pointer to the gate-site guard is dropped with it.
- **Also removed** (zero live readers): `subdir_levels`, `subdir_excludes`, `userid`
  + their CLI flags (`--subdirlevels`/`--subdirexcludes`/`--max-pixel-limit`) + TOML
  keys. **Removed from the Lua config** (parse-only, re-exposed to scripts):
  `ssl_certificate`, `ssl_key`, `keep_alive`, `logfile`. **Kept** (script-facing, read
  by Lua route scripts via the `config` table): `hostname`, `ssl_port`.
- **Both offset-locked structs re-locked:** `SipiServerConfig` 280→**240**,
  `SipiMetricsSnapshot` 176→**168**; `metrics_registry_test.cpp` inventory 22→**21**
  (drop `image_too_large_total`).
- **Bug fix folded in — the engine log level was silently ignored.** `loglevel` was
  forwarded CLI/env/TOML into `SipiConf`, but `set_log_level()` (the only mover of
  the logger gate) was never called at startup, so the engine always logged at
  `INFO`. `sipi_init` now applies it via a new `parse_log_level()`, so
  `--loglevel`/`SIPI_LOGLEVEL` (and TOML `[logging] level`) take effect. `loglevel`
  is dropped from the Lua config (engine/operational knob). Entangled with the
  removal across shared files, so it lands inside this `refactor` commit with the fix
  called out, not a standalone `fix:` line.

### Deploy-checklist deltas (fold into the ops-deploy / dsp-api PRs below)

- ops-deploy: the mode flip value is now `DSP_IIIF_ADMISSION_MODE=advanced` (not
  `enforce`); a stale `monitor`/`enforce` silently degrades to `basic`, so the value
  migration must land with the deploy. Do **not** set the removed Lua
  `nthreads`/`max_waiting_connections`/`queue_timeout` keys — use the
  `SIPI_NTHREADS`/`SIPI_MAX_WAITING`/`SIPI_QUEUE_TIMEOUT` env vars. Drop
  `DSP_IIIF_MAX_PIXEL_LIMIT` if ever set (the output-size guard is gone). The two
  memory vars were also renamed — `DSP_IIIF_MEMORY_LIMIT`→`DSP_IIIF_CONTAINER_MEMORY_LIMIT`
  (cgroup cap) and `DSP_IIIF_MEMORY_BUDGET`→`DSP_IIIF_MEMORY_LIMIT` (envelope); see the
  ops-deploy subsection below.
- dsp-api: the `sipi_image_too_large_total` metric is **removed** — drop any panel
  referencing it. If a `sipi.admission.mode` legend labels `0=monitor`/`1=enforce`,
  relabel to `basic`/`advanced` (numeric mapping unchanged).

### ops-deploy `sipi-v5-config-alignment` — v5 config landed + memory-var rename

The ops-deploy PR is **done** (branch `sipi-v5-config-alignment`, commits `62db4add`
v5 alignment + `453e8752` memory-var rename), pushed, not yet merged/applied. It drops
`rate_limit_*` and `max_decode_memory` from the Lua template, wires the `SIPI_*` env
vars, applies the engine log level, and renames the two memory variables:

- `DSP_IIIF_MEMORY_LIMIT` → **`DSP_IIIF_CONTAINER_MEMORY_LIMIT`** — the Docker cgroup
  cap (`deploy.resources.limits.memory`); 2G/4G/8G per host.
- `DSP_IIIF_MEMORY_BUDGET` → **`DSP_IIIF_MEMORY_LIMIT`** — the SIPI decode-memory
  envelope (`0`=auto), rendered into `SIPI_MEMORY_LIMIT`. The compose mapping is now 1:1.

**Cgroup-detection correction (supersedes the plan's resource-model premise).** SIPI v5
`detect_available_memory()` reads the cgroup limit first (v2 `/sys/fs/cgroup/memory.max`
→ v1 → `/proc/meminfo`), so `SIPI_MEMORY_LIMIT=0` auto-detects the container cap that
`DSP_IIIF_CONTAINER_MEMORY_LIMIT` sets — not the VM RAM. The plan's repeated "only sets
the inert Docker limit / `0`→detect overshoots to VM RAM" worry is resolved:
`DSP_IIIF_MEMORY_LIMIT="0"` on every host is correct. Note the 75/25 split is internal
to the envelope (full-lane budget = envelope × 0.75, tile reserve = 0.25); it is **not**
a haircut that leaves 25% of the cgroup unused.

**Rollout decision (maintainer, 2026-08-18): ship prod straight to `advanced`.**
`DSP_IIIF_ADMISSION_MODE` is `advanced` as the default **and** in `host_vars/vre-prod-01.yml`.
This deliberately skips the plan's original `basic`-mode shadow-validation-first
rollout — the OOM incident is severe enough that enforcing (503/413) immediately beats
another kill. The `reserve ≥ observed floor` invariant is now verified in prod under
`advanced` rather than in a prior `basic` window.

## Session handoff (2026-08-18 PM) — current state to resume from

**One-line status:** all sipi + ops-deploy code is written and pushed; the sipi PR is
open; nothing is merged or deployed yet. Remaining work is one review/merge, one
operator-applied deploy, and one unstarted dashboard PR.

### What is done and where it lives

| Repo | Branch / PR | State |
|------|-------------|-------|
| sipi | [sipi#783](https://github.com/dasch-swiss/sipi/pull/783), branch `worktree-two-lane-implementation` | **OPEN**, 9-commit stack, all gates green locally (CI gates the tip). Not merged. |
| ops-deploy | branch `sipi-v5-config-alignment` (`62db4add` v5 align + `453e8752` var rename) | **Pushed, not merged/applied.** Operator (Lukas) reviews + applies. |
| dsp-api | [dsp-api#4259](https://github.com/dasch-swiss/dsp-api/pull/4259), branch `feat/sipi-admission-dashboard` | **OPEN**, single commit `chore(grafana)`. Dashboard refactored to the `sipi_admission_*` set + validated. Not merged. |
| dasch-specs (this plan) | branch `docs/sipi-cost-based-admission-control-plan` (`c856fc9`) | Pushed. |

### What happened this session (2026-08-18 PM)

- Traced the ops-deploy env mapping: `DSP_IIIF_MEMORY_BUDGET`→`SIPI_MEMORY_LIMIT`
  (decode envelope), `DSP_IIIF_MEMORY_LIMIT`→Docker cgroup cap. Found the names were
  confusing.
- **Renamed on `sipi-v5-config-alignment`** (`453e8752`, pushed): `DSP_IIIF_MEMORY_LIMIT`
  → `DSP_IIIF_CONTAINER_MEMORY_LIMIT` (cgroup cap) and `DSP_IIIF_MEMORY_BUDGET` →
  `DSP_IIIF_MEMORY_LIMIT` (envelope). 8 files. Also fixed the misleading "75% of
  container RAM" comments.
- **Confirmed SIPI v5 detects the cgroup** (`detect_available_memory()`,
  `src/ffi/startup.cpp:58`: cgroup v2 → v1 → `/proc/meminfo`). This invalidated the
  plan's old "sees whole VM RAM / `0`→overshoots to VM RAM" premise, now corrected
  throughout.
- **Updated this plan** (`30c1e54` + `c856fc9`): cgroup-detection correction, ops-deploy
  rename recorded, ops-deploy checklist marked done, `advanced`-mode rollout decision
  recorded (ship prod straight to `advanced`).

### To resume — remaining work, in order

1. **Get [sipi#783](https://github.com/dasch-swiss/sipi/pull/783) reviewed and merged.**
   Rebase-merge (no squash); the 9 commits land verbatim. This is the gate for
   everything else.
2. **Ship a binary** carrying the new knobs (release/Docker publish per the normal SIPI
   release flow).
3. **Apply ops-deploy `sipi-v5-config-alignment`** — operator-only. **Ordering hazard:**
   do NOT apply before step 2, or compose passes `SIPI_ADMISSION_MODE=advanced` + the
   renamed vars to a binary that doesn't understand them. Prod goes straight to
   `advanced` (enforce) — watch Grafana for 503/413 and the `reserve ≥ observed floor`
   invariant right after.
4. **dsp-api dashboard PR — ✅ OPEN as [dsp-api#4259](https://github.com/dasch-swiss/dsp-api/pull/4259)**
   (branch `feat/sipi-admission-dashboard`, single commit `chore(grafana)`). Refactored
   `grafana-dashboards/sipi/sipi-iiif-media-server.json` to the shipped `sipi_admission_*`
   set: full_in_use vs max, per-partition waiting/shed, `classifier_disagreement`, 413/503
   error split, decode estimate p50/p95/p99, allocator relabel (arena/retained now true
   RSS) + scope fix, config-fingerprint row; removed the Rate Limiting + Empty-on-OTLP rows
   (and the `sipi_image_too_large_total` panel); relabeled the `sipi.admission.mode` legend
   to `basic`/`advanced`. Shadow counters intentionally **not** plotted (advanced-everywhere
   → they read ~0; maintainer decision). Also fixed `grafana-dashboards/CLAUDE.md`
   (NOT_BRIDGED / Empty-on-OTLP paragraph). Validated: jq parse, layout↔elements ref parity
   (33/33), all 39 `sipi_` refs map to `metrics.rs`, live-probed surviving queries on
   `grafanacloud-prom`. **Git Sync only pulls `main`**, so the new panels stay empty until
   sipi#783's binary ships + ops-deploy applies — hold the merge until then, or merge and
   accept a brief empty-panel window.

### Open questions / watch-items

- No open decisions. `advanced`-default and the memory-var names are both settled.
- After prod runs `advanced`: verify `(envelope − full_mem) ≥ observed floor` on the
  live `memory_limit="0"` auto path (Acceptance Criteria), and zero OOM-kills under
  crawler load (Success Metrics). These close only post-deploy.

## Session handoff (2026-08-18 late) — consistency review + doc fixes

Ran a 4-reviewer pass (consistency, rust, cpp, observability) over the full sipi#783
stack, focused on cross-round consistency. **Verdict: the admission-control code and
every enforced invariant are correct** (acquisition order, per-partition accounting,
RAII release, ABI `static_assert`s on both sides, the metrics bridge, temporality,
cardinality all verified). Every defect was **doc/glossary/comment drift** written
mid-stack and never revisited after a later commit changed ground truth.

**16 findings, 15 fixed**, each folded into its originating commit (by last-toucher,
via `git rebase --autosquash`) so the stack stays 9 self-consistent commits. Gates
re-verified green on the new tip; force-pushed. **sipi#783 is now at `3a91282c`**
(the fold rewrote `01b38777`→`31cef8bb`, `0374bdb0`→`5eaf198c`, `e443a26f`→`f56f5ebd`,
`aa007c7d`→`3a91282c`; `e1f03533`/`ce02be22` unchanged).

Fixed:
- **Critical** — the removed Output-size guard / `max_pixel_limit` (deleted in the
  removal commit) was still documented as a live Throttling sub-policy in the canonical
  docs; scrubbed from `UBIQUITOUS_LANGUAGE.md`, `ARCH-MAP.md`, `CONVENTIONS.md`, ADR-0008.
- **Vocabulary (W1)** — the "lane" vs "partition" contradiction resolved by **relaxing
  the glossary** to accept "lane" as an informal synonym (maintainer decision), rather
  than scrubbing "lane" from code/`--help`. No `--help` churn.
- **Warnings** — stale `sipi_pool_*` metric names in `sipi.md`; the Lua-config claim for
  `memory_limit`/`tiles_memory_ratio`/`admission_mode` in `memory-budget.md` + ADR-0022
  (removed from Lua in `e443a26f`); `22`→`21` and `14`→`13` count comments; residual
  `enforce`/`monitor` vocabulary in `metrics.h`, `test/e2e/BUILD.bazel`,
  `testing-strategy.md`; the 4 admission/memory flags added to the `running.md`
  Server-Options table; and **a new `AdmissionConfigReflectsEngineContext` unit test**
  covering the 4 previously-untested seam getters (`sipi_admission_mode` et al.).
- **Suggestions** — ARCH-MAP `image` depends-on `memory-budget`→`throttling`; ADR-0008
  survivor list; stale `acquire_or_shed` doc ref in `concurrency.rs`; `[[nodiscard]]` on
  `parse_log_level`.

**1 deferred — S3** (`ARCH-MAP.md` `last_verified_commit`): resolved by the dune pass
below, not by stamping a SHA.

**Follow-up dune review (2026-08-18).** A `/dune:review` over the stack (against
`CONTEXT.md` + the glossary as agent-legibility surfaces) caught two governing-doc
drifts the row-by-row consistency pass missed, plus the map-stamp item: **DUNE-002**
(`CONTEXT.md` still defined Throttling as "Decode memory budget + Output size guard" —
guard removed, Admission absent) and **DUNE-003** (the `UBIQUITOUS_LANGUAGE.md` worked-
example dialogue still walked "output-size guard first, returns 400"). Both fixed and
folded. **DUNE-001** resolved by setting `last_verified_commit: none` — a SHA is
unmaintainable under rebase-merge (the merge SHA is unknowable pre-merge; a branch SHA
is rewritten on merge), so the map tracks freshness by `date`, not SHA (maintainer
decision). **DUNE-004** (throttling local-context kit is 9 files > the ≤7 budget) left
for a `/dune:map` pass. **sipi#783 is now at `2bfd8acb`** (the DUNE-002/003/001 fold
rewrote the tip: `a882ee56` carries the glossary + map edits, `2bfd8acb` the CONTEXT.md
edit). All prior gates still green (docs-only edits).

## Implementation Phases

**Delivery model — one PR per repo.** The remaining sipi work (former PR2/PR3/PR4)
lands as a **single sipi PR** built from ordered, self-contained commits — one per
phase below. Each commit must be green on its own (all gates pass) so the PR reviews
commit-by-commit and rebase-merges verbatim; commit the ABI/engine change before the
shell that depends on it. ops-deploy and dsp-api each get their **own** PR (a single
commit each), since a PR cannot span repositories. PR1 and PR1.5 already shipped as
separate merged PRs, recorded below for history.

Commit subjects follow SIPI conventions (concern-based scope, type = the subject's
dominant change; see `docs/src/development/commit-conventions.md`). If no existing
scope fits the admission-control concern, confirm a new scope with the maintainer
before coining one.

### PR1 — Remove per-IP rate limiter + shrink config ABI ✅ DONE
Merged as [sipi#777](https://github.com/dasch-swiss/sipi/pull/777) (`refactor(handlers)`),
CI green. `SipiServerConfig` 296→256, `SipiMetricsSnapshot` 200→160; deploy-safe (Lua
reader ignores unknown keys).

#### sipi
- [x] Delete `SipiRateLimiter` + all wiring, metrics, config surface, e2e/differential
- [x] Relock both offset-locked FFI structs on C++ and Rust sides
- [x] Bump `EXPECTED_E2E_TESTS` drift guard; gates + CI green

### PR1.5 — Rate-limiter docs cleanup ✅ DONE (merged)

Merged as [sipi#779](https://github.com/dasch-swiss/sipi/pull/779) (`docs(handlers)`). PR1
removed the rate limiter in code but deferred docs; this cleaned them up.

#### sipi
- [x] Delete the rate-limiter operation doc (`docs/src/operation/rate-limiter.md`) + its `mkdocs.yml` nav entry
- [x] Supersede `docs/adr/0008-rate-limit-post-cache.md` (retained as history — its post-cache gate decision still governs the two surviving policies)
- [x] Excise stale references: `running.md` (dead `rate_limit_*` rows), `memory-budget.md` (reworded), `CONVENTIONS.md` (429 row + `SipiRateLimiter` naming example), `UBIQUITOUS_LANGUAGE.md` (Throttling umbrella → two sub-policies)
- [x] Docs build clean (mkdocs `--strict`, zero new warnings); commit-lint + CI green

### sipi PR — cost-based two-lane admission control

One PR, four ordered commits (each independently green). The move-only refactor
goes first so the behavior commits build on the new paths. Run
`bazel-rustfmt-check` + `bazel-clippy-check` + tests on each commit as you stack it.

#### Commit 1 — carve `src/throttling/cpp` (move-only refactor)
- [x] Move `SipiMemoryBudget.{h,cpp}` + `SipiPeakMemory.h` → `src/throttling/cpp/` as a new `cc_library` `//src/throttling/cpp:memory_budget`, carved out of `//src:engine` (include prefix per the iiifparser pattern; `//src:engine` depends on the new target)
- [x] Update includers (`src/ffi/serve_image.cpp`, `src/ffi/init.cpp`) and relink the memory-budget unit test against the narrow target instead of `//src:sipi_lib` (ADR-0003); tests colocated into `src/throttling/cpp/` (ADR-0003)
- [x] No behavior change: pure move + BUILD wiring; all gates green (`//src/throttling/cpp:memory_budget_test`, `//src/cli:sipi`, `//src/cli-rs:sipi` all build; commit-lint green) — committed `refactor(throttling)`

#### Commit 2 — Engine full-lane memory budget + admission mode ✅ DONE (`ce02be22`)
Naming decisions (maintainer, 2026-08-16): scope `throttling`; drop "lane" as a term, use **admission** everywhere; engine enum renamed `MemoryBudgetMode`→`AdmissionMode`. Runtime metrics (Commit 4) will be `sipi_admission_*`, not `sipi_*_lane_*`.
- [x] ABI: rename `max_decode_memory`→`memory_limit` (envelope; `0`=detect RAM) and `decode_memory_mode`→`admission_mode`; add `tiles_memory_ratio` (f64 + `has_`) and `large_decode_threshold_bytes` (u64 + `has_`; single definition in shell config, engine reads it from the seam — DUNE-003); recompute `static_assert` offsets in `sipi_ffi.h` + the `layout` test in `config.rs` (256→280 bytes, both sides)
- [x] Delete the `OFF` value from the mode enum (now `AdmissionMode`); fail loud (startup error) on an unrecognized mode string
- [x] `memory_limit` (`SIPI_MEMORY_LIMIT`; ops-deploy `DSP_IIIF_MEMORY_LIMIT`) is the single memory knob (`0`→auto-detect the container cgroup limit); drop the legacy 75%-auto path; derive `full_mem = envelope × (1 − tiles_memory_ratio)`
- [x] Charge only full-lane decodes against the budget in `serve_image.cpp` (classify by `estimate_peak_memory ≥ large_decode_threshold_bytes`); tiles bypass
- [x] Keep memory saturation an **immediate 503** (one-shot `try_acquire`); no blocking wait
- [x] Return **413** (no `Retry-After`) + distinct metric when a single estimate exceeds `full_mem`
- [x] Publish all new budget metrics (413-vs-503 counters, shadow counts) at the `serve_image.cpp` acquire site — `SipiMemoryBudget.{h,cpp}` stays free of observability includes (DUNE-005). Snapshot bridge deferred to Commit 4 per plan; the new counters are engine-internal until then
- [x] Ensure `MemoryBudgetGuard` releases on every exit path incl. exception + mid-decode client disconnect (guard scope-exit + on-exception unit tests)
- [x] Plumb `admission_mode`/`tiles_memory_ratio` through `SipiConf`, `config.rs`, `config_file.rs`, cli-rs args
- [x] Unit tests: RAII-on-panic + on-disconnect; 413-vs-503 distinction; monitor shadow-count; offset/layout tests — all green (`bazel-test-unit` 26/26)
- [x] Docs: update `docs/src/operation/memory-budget.md` for the full-lane budget, `admission_mode`, and the single `SIPI_MEMORY_LIMIT` knob (`0`=detect RAM); also `running.md` renamed rows + sample Lua config

#### Commit 3 — `admission` crate (two-lane pool) + shell integration ✅ CODE+DOCS DONE (`01b38777`); e2e deferred
_Vocabulary = admission, no "lane"; API `Admission` + `AdmissionKind {Tile,Full}`; memory-coupled config single-sourced engine-side and read back over 4 seam getters (maintainer decisions 2026-08-16)._
- [x] Create `src/throttling/rust/` (`//src/throttling/rust:admission`, `rust_library`): FFI-free — deps `tokio` + `//src/iiifparser/rust:iiif_parser` only; verify with `bazel query 'deps(//src/throttling/rust:admission)'` (no `//src/ffi`, no C++ engine) (DUNE-002)
- [x] Concrete `Admission` type built from plain config values (`nthreads`, `tiles_thread_ratio`, `tiles_memory_ratio`, `admission_mode`, `max_waiting`, `queue_timeout`, `large_decode_threshold_bytes`); no trait, no port
- [x] `classify(...) -> Lane` from domain `IiifParams`/`RequestKind` via the pixel-proxy of `large_decode_threshold_bytes` — never in the `iiif_parser` crate (ADR-0021)
- [x] `acquire(Lane) -> Permit` (RAII): encapsulates full-lane sub-semaphore (`full_max`) + global pool, **full-lane permit first, then global** for fulls; tiles take global only
- [x] Per-lane wait/shed accounting **inside the crate**: today's `WAITING`/`LOAD_SHED_TOTAL`/`QUEUE_TIMEOUT_TOTAL` module statics in `routes.rs` move into `Admission` (single writer); tiles exempt from the full-request queue-depth shed; counters exposed as a snapshot for the OTLP bridge
- [x] Lane policy for info.json/knora.json (tile), `/file` (tile), Lua routes (global-only), redirect/HEAD/passthrough/cache-hit (per Technical Considerations) encoded in the crate's classify/acquire surface
- [x] Validate ratios ∈ (0,1); clamp `tile_min`/`full_max` ≥ 1; disable lanes for `nthreads < 2`; fail loud otherwise (construction-time errors in the crate)
- [x] Shell integration: `AppState` owns the `Admission` instance; `routes.rs` reduced to `classify`/`acquire` call sites (both `acquire_or_shed` sites); new shell serve knobs (`tiles_thread_ratio`, ratios, mode, threshold) → `AppState::load` incl. `config_file.rs` + exhaustive cli-rs `ServerArgs` forwarding; single-source `admission_mode` to the engine over FFI
- [x] Config-fingerprint metrics via `metrics.rs` reading the crate: `sipi_tiles_thread_ratio`, `sipi_tiles_memory_ratio`, `sipi_admission_mode`, `sipi_tile_min_threads`, `sipi_full_max_threads`, `sipi_large_decode_threshold_bytes`, `sipi_memory_limit_bytes`
- [x] Crate-local unit tests (engine-free, fast): starvation-inversion (fulls saturate queue, tiles stay flat); 9th-tile-bursts-while-full-idle; queue-segregation; degenerate ratios; classifier unit coverage
- [x] e2e tests (shell-level): `test/e2e/tests/admission_control.rs` — enforce-mode 413, tile bypass, in-budget 200, monitor observe-only (each on its own empty cache dir); replaces the stale `memory_budget.rs`. Transient 503 stays engine-unit-covered (a live-server 503 is an unavoidable decode-timing race); classifier-disagreement counter landed shell-side in the observability commit
- [x] Docs: add a `docs/src/operation/` two-lane admission-control page (`SIPI_*` knobs, tile floor + burst, full hard cap, monitor→enforce, metrics) and an ADR for the design (the `src/throttling/{cpp,rust}` polyglot shape, package-carries-umbrella / crate-carries-mechanism naming, and why the output size guard stays inline at the gate)
- [x] Docs: update `docs/src/guide/running.md` + the config reference with the new `SIPI_*` env vars / CLI flags / Lua keys
- [x] Docs: extend the Throttling umbrella in `UBIQUITOUS_LANGUAGE.md` with the two-lane admission pool as the third sub-policy; define **Tile lane** / **Full lane** / **Admission mode** as canonical terms (DUNE-004)
- [x] Docs: register a `throttling` component in `ARCH-MAP.md` (superseding the `memory-budget` entry; kit, boundary rules, durable state, and a pointer to the inline output-size guard at the gate; via `/dune:map`) and refresh `last_verified_commit` (DUNE-001/004)

#### Commit 4 — Runtime admission metrics + allocator remap (scope: `observability`) — ✅ DONE (metrics `7134eea7`, allocator `9d7f69ff`, docs `892fa6fe`; the lint-gate + snapshot fixes folded into Commit 3 `01b38777` per commit-conventions)
> **Naming correction (maintainer 2026-08-16):** use `admission`, not `lane`. The
> metric names below are superseded — emit `sipi_admission_full_in_use`/`_max`,
> `sipi_admission_full_rejected_total` (real in enforce, shadow in monitor —
> `full_shadow_rejected_total` is already in `AdmissionSnapshot`),
> `sipi_admission_{tile,full}_waiting`/`_shed`,
> `sipi_admission_classifier_disagreement_total`. The `AdmissionSnapshot` already
> exposes per-partition occupancy/waiting/shed + `full_shadow_rejected_total`;
> `metrics.rs` currently emits the aggregate + fingerprint, so Commit 4 adds the
> per-partition series. The engine 413/503 counters `decode_memory_too_large_total`
> + `decode_memory_shadow_too_large_total` exist in the singleton (Commit 2) and
> need bridging here.
- [x] Runtime per-partition metrics (`admission` names, not `lane`): `sipi_admission_full_in_use`, `sipi_admission_{tile,full}_waiting`/`_shed`, `sipi_admission_full_shadow_rejected` (monitor would-be full-cap; real enforce sheds are `full_shed`), and the shell-only `sipi_admission_classifier_disagreement` (compares the shell verdict against the engine's `SipiServeTimings.decode_estimate_bytes` — no ABI change). `feat(observability)` commit `7134eea7`
- [x] Bridge the engine 413 counters (`decode_memory_too_large_total` + shadow) through `SipiMetricsSnapshot` (`metrics_snapshot.h` + `sipi_ffi.cpp` + `ffi.rs`), relock 160→**176** on both sides, and bump the `metrics_registry_test.cpp` inventory 20→22
- [x] Remap `sipi_malloc_arena_bytes`→`mi_process_info current_rss`, `retained_bytes`→`current_rss − in_use` in `mi_stats_shim.c` + `main.rs` + `malloc_stats.rs`; own `fix(observability)` commit `9d7f69ff`. Linux/mimalloc-only, CI-verified (macOS links no mimalloc)
- [x] Temporality documented: the `sipi_admission_*` counters are cumulative process-lifetime sums (`rate()`/`increase()` correct), unlike the session-scoped counters that need `max_over_time()` — noted in admission-control.md
- [x] Docs: per-partition + fingerprint metrics and the RSS remap documented in admission-control.md + memory-budget.md; `docs(observability)` commit `892fa6fe`
- [x] Also fixed: `//src/throttling/rust` (added in Commit 3) was absent from the `bazel-rustfmt-check`/`bazel-clippy-check` recipes — added it, folded into Commit 3 `01b38777`

### ops-deploy PR — config consolidation + advanced flip (author only; operator applies) ✅ DONE (branch `sipi-v5-config-alignment`)

Landed on branch `sipi-v5-config-alignment` (commits `62db4add` v5 alignment +
`453e8752` memory-var rename), pushed, **not yet merged/applied** — operator (Lukas)
reviews + applies. See the 2026-08-18 ops-deploy subsection above.

#### ops-deploy
- [x] Drop `rate_limit_*` + `max_decode_memory` from `roles/dsp-deploy/templates/iiif/conf/sipi.prod-config.lua.j2` + `defaults/main.yml`
- [x] Two-lane knobs as env/TOML overrides (defaults in code). No removed Lua `nthreads`/`max_waiting_connections`/`queue_timeout` keys — `SIPI_NTHREADS`/`SIPI_MAX_WAITING`/`SIPI_QUEUE_TIMEOUT` env vars. `admission_mode`/`memory_limit`/`tiles_memory_ratio` set via env/TOML, not Lua. No `DSP_IIIF_MAX_PIXEL_LIMIT` (output-size guard gone)
- [x] Wire `SIPI_MEMORY_LIMIT={{ DSP_IIIF_MEMORY_LIMIT }}` (decode envelope) and set the container cgroup cap via the renamed `DSP_IIIF_CONTAINER_MEMORY_LIMIT` (`deploy.resources.limits.memory`). `0`→auto now detects that cgroup cap, not VM RAM
- [x] `DSP_IIIF_ADMISSION_MODE=advanced` set as the default **and** in `host_vars/vre-prod-01.yml`. A stale `monitor`/`enforce` silently falls back to `basic`. Ships prod straight to `advanced` by maintainer decision (2026-08-18), skipping the `basic`-mode shadow window — see the 2026-08-18 subsection
- [ ] Docs: ops-deploy config reference/README for the consolidated memory knob + `DSP_IIIF_ADMISSION_MODE` (`basic`/`advanced`)/ratio variables (verify on the branch)

### dsp-api PR — dashboard refresh ✅ OPEN as [dsp-api#4259](https://github.com/dasch-swiss/dsp-api/pull/4259)

Own PR (single commit `chore(grafana)`, branch `feat/sipi-admission-dashboard`). Not merged.

#### dsp-api
- [x] Refactor `grafana-dashboards/sipi/sipi-iiif-media-server.json`: kept Overview/Latency/Auth/Cache; improved Decode Memory (full-lane used vs budget + estimate p50/p95/p99), Allocator (post-remap relabel + scope fix), Pool Saturation → **Admission Saturation** (per-partition + full-lane hard cap + classifier disagreement), Errors (413/503 split); removed Rate Limiting row; reframed Resources (working_set vs envelope overlay); deleted the "Empty on OTLP" row; added a **Config Fingerprint** row. `sipi_image_too_large_total` panel dropped; `sipi.admission.mode` legend relabeled `0=basic`/`1=advanced`. Shadow counters not plotted (advanced-everywhere). Also fixed `grafana-dashboards/CLAUDE.md`. Validated (jq, ref parity, metric-name audit vs `metrics.rs`, live probe)

## Acceptance Criteria

- [ ] Under a synthetic full-burst with tiles idle, then a tile burst: tile p99 latency stays flat; a 9th tile is admitted while full threads sit idle
- [ ] Full lane never exceeds `full_max` threads or `full_mem` bytes; excess fulls get 503 + `Retry-After` (or 413 for single-request-exceeds-budget)
- [ ] Tiles are never shed due to full-request queue pressure (per-lane accounting verified)
- [ ] `MemoryBudgetGuard` releases on panic and on mid-decode client disconnect (tested)
- [ ] Degenerate configs (ratio 0/1/out-of-range, `nthreads`=1) are validated: fail loud or documented single-lane fallback
- [ ] `admission_mode` is single-sourced (engine-resolves / shell-reads-back); a mismatched/legacy value **falls back to `basic`** (silent, no startup error — superseded 2026-08-18)
- [ ] Fingerprint metrics (incl. `sipi_memory_limit_bytes` and both thresholds) render on Grafana **with no ops-deploy change** after shipping the binary
- [ ] `basic`-mode shadow counters exist for both the memory and thread lanes, sufficient to size `full_max`/`full_mem` before flipping to `advanced`
- [ ] `(envelope − full_mem) ≥ observed floor` verified in `basic` against the live `memory_limit="0"` auto path (not just the 8G example)
- [x] A dedicated advanced-mode e2e suite covers 413 (`admission_control.rs`; `basic` observe-only case included); the differential gate was removed with the oracle (ADR-0020). Transient 503 is engine-unit-covered rather than e2e — a live-server 503 is an unavoidable decode-timing race (documented in the suite's module doc)
- [ ] `//src/throttling/rust:admission` is engine-free: `bazel query 'deps(//src/throttling/rust:admission)'` shows no `//src/ffi` and no C++ engine; no semaphore, lane, ratio-validation, or admission-counter static remains in `routes.rs` (DUNE-002)
- [ ] The move-only carve (Commit 1) changes no behavior: `//src/throttling/cpp:memory_budget` exists, `//src:engine` depends on it, and the memory-budget unit test links the narrow target
- [ ] `large_decode_threshold_bytes` has exactly one definition site (shell config); the engine reads it from the seam struct; the fingerprint metric exports that value (DUNE-003)
- [ ] `SipiMemoryBudget.{h,cpp}` includes no observability header (DUNE-005)
- [ ] `UBIQUITOUS_LANGUAGE.md` lists the two-lane pool under Throttling (Tile lane / Full lane / Admission mode defined); `ARCH-MAP.md` carries a `throttling` component entry (superseding `memory-budget`) with a refreshed `last_verified_commit` (DUNE-001/004)
- [ ] Docs updated: rate-limiter docs removed; admission-control + memory-budget docs reflect the new model; running/config reference lists the `SIPI_*` knobs; design ADR added
- [ ] All gates green on every commit of the sipi PR (and on each of the ops-deploy / dsp-api PRs): `bazel-rustfmt-check`, `bazel-clippy-check`, `bazel-test-unit`, `bazel-coverage`, `commit-lint`

## Dependencies & Risks

- **Cross-repo deploy ordering:** the sipi PR ships with no ops-deploy change (Lua ignores
  unknown keys). The ops-deploy PR drops dead keys + adds the `advanced` flip; operator applies.
  Order: merge the sipi PR → ship a binary with the new knobs → then the ops-deploy + dsp-api PRs.
- **No differential gate:** the C++ oracle and its parity gate were removed (ADR-0020).
  `advanced`-mode 503/413 must be covered by dedicated e2e tests — there is no second
  implementation to diff against.
- **macOS CI flakes:** two known classes — the Apple-SDK-403 toolchain fetch (PR1 hit it;
  clears on rerun) and the nix-bash `setlocale` segfault (fixed via `.bazelrc
  --test_env=LC_ALL=C`). Don't misdiagnose as regressions.

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Acquisition-order inversion starves tiles | M | H | Full-lane-first ordering; starvation-inversion regression test |
| Shared wait queue sheds tiles under full burst | M | H | Per-lane wait accounting; tile exemption from queue-depth shed |
| Classifier disagreement (shell vs engine) | H | M | One threshold defined shell-side, passed over the seam at init; export disagreement counter; agreement test |
| Auto-mode (`memory_limit="0"`) budget too tight/loose | M | M | Spell out auto arithmetic; verify invariant in `basic` before `advanced` |
| `admission_mode` skew across two config surfaces | M | M | Single-source over the seam (engine resolves, shell reads back); unknown value falls back to `basic` |
| ABI offset drift on field add | M | H | `static_assert` + `layout` test are the oracle; recompute both, in the commit that touches the ABI |
| Misleading allocator gauges read as OOM proxy | M | M | Remap in the lane-metrics commit; exclude from acceptance criteria until remapped |
| Degenerate ratios → `full_max=0`, all fulls 503 | L | H | Startup validation + clamps + tests |

## Success Metrics

- Zero OOM-kills on `vre-prod-01` under crawler load (working_set stays within envelope).
- Tile p95 latency unchanged under a full-lane-saturating load (no starvation).
- `sipi_admission_full_shed`/`sipi_decode_memory_too_large_total` > 0 only under abuse (in `advanced`); the tile partition never sheds for full pressure.
- Post-remap allocator gauges track actual RSS (sanity vs `container_memory_working_set_bytes`).

## References

- Spec/design origin: this folder; PR1 [sipi#777](https://github.com/dasch-swiss/sipi/pull/777)
- Architecture: `ARCH-MAP.md` (memory-budget / observability / server-rs / iiifparser boundary rules), `docs/adr/0020-oracle-removal.md`, `docs/adr/0021-iiifparser-polyglot-colocation.md`
- Pool/admission code: `src/server-rs/src/routes.rs:100-351,1049` (current pool; moves into the planned `//src/throttling/rust:admission` crate); output size guard: **removed** (`aa007c7d`; was `serve_image.cpp:529-538` reading `max_pixel_limit`); parser: `src/iiifparser/rust/request.rs:92` (`parse_request` → domain `IiifParams`), seam mapping `src/server-rs/src/ffi.rs:217`
- Dune review findings folded in 2026-08-16: DUNE-001 (map re-verify), DUNE-002 (admission crate carve; extended by maintainer decision to the full `src/throttling/{cpp,rust}` colocation), DUNE-003 (threshold over the seam), DUNE-004 (glossary + map registration), DUNE-005 (budget metrics publish site)
- Engine budget: `src/ffi/serve_image.cpp:615-660`, `src/SipiMemoryBudget.{h,cpp}`, `src/SipiPeakMemory.h:36`
- Config/ABI: `src/ffi/sipi_ffi.h`, `src/server-rs/src/config.rs`, `src/SipiConf.cpp:145,150`
- Metrics: `src/server-rs/src/metrics.rs:123-164`; allocator: `src/cli-rs/src/main.rs:184-201`, `mi_stats_shim.c`
- Learnings: `project_sipi_prod_resource_model`, `project_sipi_memory_not_a_leak` (allocator gauges misleading), `reference_sipi_grafana_dashboard_in_dsp_api`, `promql-session-scoped-counters-max-over-time-fix`, `project_sipi_otel_cpp_never_existed`, `project_sipi_bilevel_tiff_flaky_segfault`
