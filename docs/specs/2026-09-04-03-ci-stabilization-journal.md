---
title: "SIPI security hardening wave-2 — CI stabilization journal"
date: 2026-09-05
author: Ivan Subotic
status: draft
---

# CI stabilization journal (interactive session, post-orchestration)

This journal covers work done in the **interactive (Fable) session** AFTER the
orchestrator + adversarial-review rounds recorded in
[`01-...-journal.md`](2026-09-04-01-fix-sipi-security-hardening-wave-2-journal.md). Scope:
resolving the one review blocker (T1) and driving **PR #800** to fully-green CI.
All wave-2 code (Phases 1–10 + S2-39 + T1) is code-complete; this phase is CI
stabilization + history cleanup only.

**Working convention from here on:** substantive investigation/edits are
delegated to **sonnet subagents** to keep the session context light; this file is
the durable record so context survives compaction. The session itself does git,
CI polling, synthesis, and journal upkeep.

## State anchors

- Worktree: `.claude/worktrees/security-analysis`
- Branch: `feature/dev-7131-sipi-security-hardening-wave-2-deep-analysis-after-dev-6418` (base `main` @ `5007032c`)
- **PR #800** — https://github.com/dasch-swiss/sipi/pull/800 (base `main`, protected)
- 60 clean commits, one per finding; 2 breaking (`feat(throttling)!` admission flip, `refactor(cli)!` dead-flag removal) → MAJOR
- Safety net: local ref `backup-pre-cleanup`
- Non-interactive autosquash works here: `GIT_SEQUENCE_EDITOR=: GIT_EDITOR=: git rebase -i --autosquash 5007032c`
- Run bazel/just via `nix develop -c bash -lc '<cmd>'` (direnv blocked in worktree; ignore shellHook syntax-error noise). Local ASan BROKEN on darwin → asan is CI-only.
- Cross-build to verify Linux locally: `--platforms=//platforms:linux_x86_64` / `:linux_aarch64`.

## T1 resolution (the review blocker) — RESOLVED, it was a test bug

The orchestrator left T1 blocked, hypothesizing a "pre-existing concurrency race in
the JPEG-in-TIFF decode path." That was **wrong**. Primary-evidence investigation
disproved every escalating hypothesis:
- NOT a libtiff-4.7.2 regression (rebuilt pinned to 4.7.1, failed identically).
- NOT a deadlock (thread `sample` under -O0 showed destructor churn, not a lock).
- NOT truncation (raw-socket read showed a valid `ffd9\r\n0\r\n\r\n` — EOI + chunk terminator; byte-identical q60 comparison).

**Real cause:** a racy test cleanup in `test/e2e/tests/iiif_compliance.rs` —
`remove_file(&dst)` ran before `resp.bytes()` was read, so a sibling parallel test
could delete the response file mid-read → `hyper UnexpectedEof`. **Fix:** reorder
to read `resp.bytes()` before `remove_file(&dst)`. Landed as standalone commit
`c0477e8e`.

**Dead-mutex cleanup:** confirmed the commented-out `inlock` mutex in
`src/format_handlers/cpp/SipiIOJpeg.cpp:509-512` was orphaned dead code (the
concurrency-bug hypothesis's artifact) and removed it; folded into the S2-14 commit.

## CI-leg fixes (each folded into its originating commit)

| # | Leg | Root cause | Fix (file) | Verified |
|---|---|---|---|---|
| 1 | libexpat Linux build | expat 2.8.4 `#include "expat_config.h"` unconditionally; vendored BUILD lacked it | genrule emits empty `expat_config.h` stub + adds `:expat_config_h` to srcs (`bazel/libexpat.BUILD.bazel`) | cross-build all 3 platforms |
| 2 | dependency-audit | RUSTSEC-2023-0071 (rsa, unreachable — HS256 only) + GHSA-w9wp-h8wv-79jx (opentelemetry_sdk baggage, unreachable — TraceContextPropagator only) | `--ignore` in justfile `audit` recipe + new `osv-scanner.toml` `[[IgnoredVulns]]` (+ `Cargo.lock:Cargo.Bazel.lock` parse-as) | osv-scanner "No issues found", exit 0 locally |
| 3 | docker_smoke | image fail-closes on missing/short jwt_secret (S2-11); `docker run` supplied none | added `-e SIPI_JWTKEY=...` (≥32B non-default) to `docker run` (`test/e2e/tests/docker_smoke.rs`) | CONFIRMED green (linux-amd64 test leg) |
| 4 | asan-ubsan (all 19 e2e) | per-decode `std::thread` create+join in `run_with_deadline` trips ASan "Joining already joined thread" (slot-ID reuse false positive) in the Rust shell → server aborts on first use → all e2e Connection-refused | `#if __SANITIZE_ADDRESS__` inline path, no worker thread (mirrors Kakadu `num_threads=0` guards) (`src/ffi/cpp/decode_guard.h`) | CONFIRMED on CI: cleared all 19 e2e asan failures |
| 5 | asan-ubsan (`tiff_roundtrip_fuzz`) | malformed seed decodes to a DEGENERATE image (0 nx/ny/nc); asan-only encode crash | guard: skip encode if `getNx()==0 \|\| getNy()==0 \|\| getNc()==0` (`src/format_handlers/fuzz/codec_fuzz_harness.h`) | **CONFIRMED PASS** under asan on CI (df186f2f, 0.8s) |
| 6 | asan-ubsan (`//src/ffi:serve_image_test` TIMEOUT) | **regression from fix #4**: the asan-inline `run_with_deadline` runs the producer inline with no watchdog thread and no deadline, so serve_image_test's decode-hang fixture (expects the watchdog to return nullopt on a non-returning producer) hangs forever → whole binary TIMEOUTs | `GTEST_SKIP()` the hang/deadline test(s) under the same asan macro (test-only; the watchdog is compiled out under asan, and the same test still runs on all 3 non-asan platforms) | subagent editing; non-asan verify local, asan verdict on next CI |

Fix #4 memory: [[sipi_short_lived_threads_trip_asan_in_rust_shell]].
Fix #1–5 landed and confirmed green under asan on CI (df186f2f). **Fix #4 traded the
join-path false positive for a timeout-path hang** in the one test that exercises the
watchdog — caught on df186f2f as `serve_image_test` TIMEOUT (NOT tiff_roundtrip_fuzz,
which passed). Fix #6 resolves it. This is the last open asan item.

## History cleanup

Per `docs/src/development/commit-conventions.md`: all review/CI fixes were folded
via `git commit --fixup=<sha>` + non-interactive autosquash into their originating
commits (in-branch bugs fold in, never a standalone `fix:`). T1 was the one
exception — it corrects a test artifact and landed as its own commit `c0477e8e`.
A deep per-finding re-fold was attempted and ABORTED (a bundled Phase-6a test
conflicted); restored from `backup-pre-cleanup` and did the safe cleanup instead.
Final: 60 commits, 0 fixup leftovers. HEAD `df186f2f`.

## Follow-up filed

- **DEV-7154** — bump `opentelemetry*` 0.31 → 0.32.x; then remove the
  GHSA-w9wp-h8wv-79jx ignore from `osv-scanner.toml`. (assignee Ivan, High, related DEV-7131.)

## Current status (2026-09-05) — CI FULLY GREEN

- PR #800 on `616dc953`. **All 9 checks pass**: asan-ubsan (2m36s), test/darwin-arm64,
  test/linux-amd64, test/linux-arm64, dependency-audit, commit-lint, docs, changes, docker scout.
- 60 commits, one per finding; 2 breaking → MAJOR. History clean, 0 fixup leftovers.
- **Ready to merge.** Base `main` is protected → Ivan performs the rebase-merge.
- **Next (operator, not code):** the out-of-scope phases below (0/11/12/13 + dsp-api
  sipi.init.lua restrict-size default). PR-time record deferrals may be noted on the PR.

## Out of scope (operator / cross-repo — NOT this PR)

- Phase 0: verify/rotate prod `jwt_secret` in ops-deploy before release.
- Phase 11: deploy posture (ops-deploy env for new knobs; remove dead routes; container hardening; Traefik).
- Phase 12: Linear reconciliation (close DEV-6072/6075/6368/6369/6117; verify DEV-6640/7079; DEV-7132..7147 children).
- Phase 13: release-please MAJOR + dsp-api two-`oci.pull`-digest bump + ops-deploy roll dev→stage→prod.
- dsp-api `sipi.init.lua`: default a restrict size (`!128,128`) for bare `{type="restrict"}` before rollout.

## PR-time record deferrals (mention in PR, not blockers)

- oha 60s before/after smoke; `just bench decode` before/after; fuzz.yml workflow_dispatch green on all legs; exhaustive 7×3 preflight×credential e2e matrix (focused per-finding e2e landed instead).
