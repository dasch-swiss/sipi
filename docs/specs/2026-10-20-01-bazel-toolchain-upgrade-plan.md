---
title: "build(bazel): upgrade Bazel 9.3, rules_rust 0.74, hermetic-llvm 0.8.25, Rust 1.98, LLVM 23"
type: refactor
date: 2026-10-20
author: Ivan Subotic
status: implemented
repositories: []
linear: DEV-7575
---

# build(bazel): upgrade Bazel and the hermetic toolchains

## Overview

Bump every toolchain SIPI pins, one step per commit, in one PR:

| Step | From | To |
|------|------|----|
| 1. Bazel | 9.1.0 | 9.3.0 |
| 2. rules_rust | 0.70.0 (+ isysroot patch) | 0.74.0 (patch dropped; upstream since 0.71.0) |
| 3. hermetic `llvm` module | 0.8.18 (+ headers glob patch), LLVM 22.1.7 | 0.8.25, LLVM 22.1.7 (patch dropped only if proven unneeded; a rebase is expected) |
| 4. Rust | 1.89.0 | 1.98.0 |
| 5. LLVM | 22.1.x | 23.1.2 |

The PR is rebase-merged as five toolchain commits (four if phase 5 is dropped), after
the `docs(specs)` commit that adds this plan. Each commit is
pushed on its own and must be green on the full CI matrix before the next phase commits.
So every step is proven green on its own head, not just the PR's final head. Steps 1, 3
and 5 change most C++ action keys. Steps 2 and 4 mainly change Rust action keys. Expect
large remote-cache rebuilds on those pushes.

## Problem Statement / Motivation

- **Bazel 9.1.0** lacks fixes that matter to our RBE setup.
  - 9.3.0 turns on `--rewind_lost_inputs` by default. An evicted NativeLink blob now
    reruns the action instead of failing the build.
  - 9.3.0 invalidates cached results when the endpoint or `--remote_instance_name`
    changes.
  - 9.3.0's Build without the Bytes re-downloads deleted outputs.
  - 9.3.0 fixes a `--downloader_config` / repository-cache failure.
  - 9.2.0 fixed a `RepoMappingManifest` CPU regression.
- **Two local patches**, `bazel/patches/rules_rust_cargo_build_script_isysroot.patch` and
  `bazel/patches/hermetic_llvm_headers_glob.patch`, need upkeep and block clean bumps.
  - Upstream made the rules_rust patch unnecessary in 0.71.0.
  - The llvm patch is probably still needed: 0.8.25 still passes the kernel and glibc
    headers as a bare source directory.
- **hermetic-llvm 0.8.25** brings ASan/UBSan runtime fixes and the sanitizer header
  overlap fix. It also adds macOS 27 SDK text-stub support (0.8.23) and stripped
  prebuilts (0.8.20).
- **Rust 1.89.0** is nine releases behind. 1.98.0 is the newest version rules_rust 0.74.0
  lists.
- **LLVM 23** cuts compile time, because libc++ 23 dropped many transitive includes. It
  also keeps us close to upstream and enables `-Wlifetime-safety` for later hardening
  work. It comes as the last step, with an explicit drop path.

## Proposed Solution

One branch with one commit per toolchain phase (1–5), plus a final verification phase (6)
that commits nothing. The phases run strictly in sequence. Every toolchain phase:

1. Makes its version change.
2. Regenerates `MODULE.bazel.lock`, and `Cargo.Bazel.lock` where relevant.
3. Updates every doc mention of the old pin as current. The check is a grep that returns
   nothing.
4. Runs the local macOS gates and commits.
5. Gets an adversarial review **before** it is pushed. A first push then reflects the
   reviewed commit, which matters for phase 3's first-attempt-green rule.

The *next* phase opens by pushing the previous commit. It runs `gh pr checks --watch`
until every `ci.yml` leg is green: linux-amd64, linux-arm64, darwin-arm64, rustfmt and
clippy (on amd64), smoke, and asan-ubsan. A CI failure is fixed with a new
`fixup! <exact subject of the phase commit>` commit, pushed normally, and CI is watched
again. History is never rewritten mid-run. At ship, the session folds every `fixup!` and
spec-state commit per `docs/src/development/commit-conventions.md` and pushes once with
`--force-with-lease` (H1). Never push two phases at once. `ci.yml` sets
`cancel-in-progress`, so a second push would cancel the first phase's run and the step
would never be verified on its own.

`ci.yml` triggers only on `pull_request` and `workflow_dispatch`. The first push
therefore also opens the PR as a draft.

Every commit uses `build(bazel): …`. These changes are build plumbing, not deployer-facing
behavior, so they do not bump the release version.

### Deciding the headers-glob patch

Per the root cause in `docs/src/development/rbe.md`, the `'linux/errno.h'` /
`'linux/limits.h' file not found` failure has one source. hermetic-llvm passes the
kernel and glibc headers to compile actions as a **directory artifact**. Remote execution
drops that directory's `include/` subtree when it builds the Merkle input tree. If every
such header reaches the action as a named file, the failure cannot occur. That check is
deterministic and needs no cold cache. It covers both input paths:

- **Path A:** the toolchain `cc_args` on target-config compiles.
- **Path B:** the `kernel_headers` / `gnu_libc_headers` `cc_library` that the exec-config
  stage0 libcxx/libunwind compiles consume.

If the patch is removed, the first CI run on the phase-3 commit is extra evidence. Removing it changes how
those headers are declared, and that changes the affected actions' keys. That run must be
green on its first attempt. A `gh run rerun` resumes from uploaded results and proves
nothing. The aquery check is still what makes the drop safe.

## Technical Considerations

- **ADR-0014 pinning policy.** The ADR (accepted) says to pin the `llvm` module exactly
  and to bump "only on a real trigger — an LLVM version we need, or a fix we require —
  never speculatively". Every bump also re-runs the full equivalence gate. This plan keeps
  that policy and names the triggers:
  - **0.8.25 fixes we require.** 0.8.24 and 0.8.25 fix the ASan/UBSan runtime and the
    sanitizer header inputs used by the `asan-ubsan` CI job. 0.8.23's macOS 27 SDK
    text-stub support prepares for moving the hermetic SDK pin past `MacOSX26.4.sdk`.
    It is secondary.
  - **LLVM 23.1.2 is an owner decision (DEV-7575 planning, 2026-10-20).** The trigger is
    `-Wlifetime-safety`, which only exists in Clang 23 and is planned for the C++
    hardening follow-up. The compile-time gain from libc++ 23 is secondary.
  - **The equivalence gate is the phase checklists.** Approval goldens unchanged, the
    3-platform build, coverage, sanitizer, fuzz, and both Docker arches. CI's smoke step
    builds and loads the image on both Linux legs.
  - Phases 3 and 5 each add an ADR-0014 update note recording the bump and its trigger,
    in the style of the existing 0.8.18 note.
- **Never run `bazel mod tidy`.** It strips the hermetic-llvm `use_repo` lines, which are
  load-bearing (ADR-0014, ADR-0015). Lockfile refreshes use `bazel mod deps`.
- **Minimal version selection (MVS) pulls deps up.** `llvm` 0.8.25 requires
  `rules_cc` 0.2.25, `bazel_skylib` 1.9.0, `rules_go` 0.62.0 and `bazel_lib` 3.2.2.
  - The root pins `rules_cc` 0.2.19 and `rules_go` 0.60.0, so MVS resolves the higher
    versions anyway. The issue's "hold rules_cc at 0.2.19" cannot survive step 3.
  - Phase 3 raises the root pins to whatever `bazel mod graph` resolves. This is hygiene:
    `--check_direct_dependencies` defaults to warn and is not a gate here.
  - Phase 3 also absorbs rules_cc 0.2.25's disallowed-copts checks and its include-path
    error.
  - A `single_version_override` downgrade is rejected, because llvm 0.8.25 may load
    0.2.25-only APIs.
  - rules_rust 0.74.0 needs `bazel_features` 1.50.0, above llvm's 1.42.0. MVS resolves
    1.50.0, which is harmless. Nothing else in rules_rust 0.74.0 is above llvm's floor.
  - The `rules_go` pin exists only to override the protobuf-pulled `rules_go`
    (`MODULE.bazel:137-144`). If llvm's 0.62.0 now supersedes it and nothing at the root
    loads `@rules_go`, the explicit `bazel_dep` goes.
- **The overrides are patch-only.** The llvm override (`MODULE.bazel:183-187`) and the
  rules_rust override (`:493-497`) follow the `bazel_dep` version, so a bump re-applies
  the old patch. A patch that no longer applies fails at fetch time.
- **LLVM pin.** ADR-0014 requires an exact `llvm.version`. 0.8.25's
  `llvm_versions.json` still lists 22.1.7, along with 22.1.8, 23.1.0 and 23.1.2. Phase 3
  keeps 22.1.7, so the module bump is not also a compiler bump. The 0.8.25 extension
  still exposes `@llvm//extensions:llvm.bzl` with `llvm.version` and the `llvm-project`
  repo.
- **The headers-glob patch is probably still needed.** At v0.8.25, the two BUILD files
  still declare the headers from the bare `include` directory:
  `kernel/extension/kernel-headers.BUILD.bazel` and
  `runtimes/glibc/extension/glibc-headers.BUILD.bazel`. `directory.bzl`'s
  `headers_directory` still forwards a single source-directory artifact. So phase 3
  expects to rebase the patch. It removes the patch only if the path A and path B checks
  show named files without it.
- **Lockfile regeneration.** rules_rust 0.74.0 moves its Cargo.lock parser to
  `cargo-lock` v11 (#4228), so expect a large `Cargo.Bazel.lock` diff.
  - The repin command is `CARGO_BAZEL_REPIN=1 bazel build //src/server/rust:lib`
    (`MODULE.bazel:624`).
  - `aws-lc-sys` and `openssl-sys` must stay out of the graph. Their cmake-driven
    `build.rs` panics inside Bazel actions.
- **The isysroot fix is upstream.** rules_rust #4101 changed
  `_pwd_flags_sysroot` in `cargo/private/cargo_build_script.bzl` to
  `_prefix_pwd_to_flag(args, ["--sysroot=", "--sysroot", "-isysroot"])`. That covers both
  the `-isysroot X` and `-isysrootX` forms. The first release with it is 0.71.0. Phase 2
  drops the patch outright, and a darwin `ring` build is the empirical check.
- **rules_rust 0.71.0 removed WORKSPACE support** and some attrs (`out_binary`,
  `is_proc_macro_dep`). 0.71.2 re-added the `crate_universe/extension.bzl` shim with a
  deprecation warning. 0.72.0 deprecates `force_all_deps_direct`.
- **Cross-compile coupling.** The arm64 and darwin legs compile on the x86_64 worker. They
  rely on `extra_target_triples`, `--host_platform` and `--noremote_local_fallback`. If
  toolchain resolution breaks, an arm64 or darwin `process_wrapper` gets shipped to the
  x86_64 worker. `--toolchain_resolution_debug` diagnoses it.
- **Cold-fetch landmines.** The Bazel 9.1.0 bump armed a `file://` NPE in custom
  repository rules. It stayed hidden behind warm repository markers
  (`dasch-specs/learnings/build-errors/bazel-repository-ctx-download-file-url-checksum-crash.md`).
  After `.bazelversion` changes, force a fresh fetch of the `gh_release_archive` repos.
- **`_FORTIFY_SOURCE` ordering.** `--config=release` undefines and redefines it after
  the toolchain's opt feature injects `=1` (`.bazelrc:105-114`). A new toolchain can
  change the flag order. Phases 3 and 5 re-check it.
- **Hot-path benchmark rule.** A compiler change alters codegen on the decode, encode and
  process hot paths. Phases 3 and 5 run benches before and after, back to back on one
  macOS machine. Phase 4 changes only Rust, so phase 3's "after" run is phase 5's
  baseline if both run in one session on one machine.
- **LLVM 23 specifics.**
  - libc++ 23 removed transitive includes. Fix them with the missing `#include`, never
    with `_LIBCPP_KEEP_TRANSITIVE_INCLUDES_LLVM23`.
  - Clang 23 does more aggressive dead-store elimination after an object's lifetime ends.
    A changed golden may therefore be latent UB that the new compiler exposes, not a
    toolchain fault. It gets triaged, not silently dropped.
  - New warnings (`-Wunused-template` under `-Wunused`, `-Wunused-but-set-global`) are
    not errors here, because the C++ build has no `-Werror`. The clean-build bar still
    applies, so first-party findings get fixed.
  - The `deque` iterator ABI change is harmless, because everything is rebuilt and the
    FFI seam is C-style.
  - LLVM 23 support for libFuzzer and the sanitizers on macOS is unverified.
  - rustc 1.98 bundles its own older LLVM, so the Rust sancov instrumentation and clang
    23's libFuzzer runtime come from different LLVMs. The macOS fuzz link may fail on
    that mismatch.

## Implementation Phases

#### Phase 1: Bazel 9.1.0 → 9.3.0

- [x] Run `gh auth status`; it shows an authenticated account with `dasch-swiss` access (Kakadu fetch prerequisite)
- [x] Set `.bazelversion` to `9.3.0`
- [x] Fix the module docstring at `MODULE.bazel:3` and the `.bazelrc:1` header, which both said "Bazel 9.0.2 LTS", to point at `.bazelversion` and name no version (owner decision 2026-10-20: one source of truth for the Bazel pin)
- [x] Regenerate `MODULE.bazel.lock` with `bazel mod deps`
- [x] Force a fresh fetch of the Kakadu repo with `bazel fetch --force --repo=@kakadu`, per the cold-fetch learning; it succeeds
- [x] Force a fresh fetch of the bench fixtures with `bazel fetch --force --repo=@sipi_bench_fixtures` (321 MB); it succeeds
- [x] Confirm `--incompatible_check_external_repo_source_dir_package_boundary` exists with `bazel help build | grep check_external_repo_source_dir`
- [x] Run `bazel build --incompatible_check_external_repo_source_dir_package_boundary //...` once, without committing the flag. If it fails, add a "Bazel 10 readiness" note to `docs/src/development/bazel.md` naming the offending external repos.
- [x] Run `grep -rn '9\.0\.2\|Bazel 9\.1\.0' --exclude-dir=docs/specs --exclude=CHANGELOG.md .`; no hit names an old version as the current pin. `bazel/gh_release.bzl:58` and `platforms/BUILD.bazel:49` are historical "Bazel 9.1.0 introduced X" statements and stay. `justfile:95` keeps the 9.1.0 history but says `.bazelversion` must stay at or above it (owner decision 2026-10-20).
- [x] Run `just bazel-build`; it succeeds with no new warnings
- [x] Run `bazel test --build_tests_only --test_tag_filters=-requires-docker` over the `just bazel-coverage` target set; all 77 tests pass, approval goldens included
- [x] `just bazel-coverage` is an unenforceable gate, recorded rather than run. Coverage instrumentation is disabled repo-wide under hermetic-llvm: `coverage.yml` is manual-only ("no DA records"), and ADR-0014 gates it on rules_cc #385. On this macOS 27 host, the hermetic LLVM 22.1.7 `llvm-profdata merge` also cannot create its output file. The `bazel test` run over the same target set (above) stands in for it.
- [x] Run `just bazel-build-fuzz`; it succeeds
- [x] Commit as `build(bazel): upgrade Bazel to 9.3.0`
- [x] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 2: rules_rust 0.70.0 → 0.74.0

- [x] Push the branch and open a draft PR with `gh pr create --draft`, title `build(bazel): upgrade Bazel and the hermetic toolchains`
- [x] Run `gh pr checks --watch`; every `ci.yml` leg is green on the phase-1 commit. Fix any failure with a `fixup!` commit for the phase-1 commit.
- [x] Bump `bazel_dep(name = "rules_rust")` to `0.74.0` in `MODULE.bazel`
- [x] Remove the rules_rust `single_version_override` and its comment block (`MODULE.bazel:483-497`); upstream #4101 (in 0.71.0) prefixes `-isysroot`
- [x] Delete `bazel/patches/rules_rust_cargo_build_script_isysroot.patch` and its export in `bazel/patches/BUILD.bazel`
- [x] Repin with `CARGO_BAZEL_REPIN=1 bazel build //src/server/rust:lib`, with Rust still at `1.89.0`. The 1.89.0 toolchain fetch succeeds under 0.74.0. If 0.74.0 no longer ships 1.89.0 checksums, stop and escalate rather than folding the Rust bump in here.
- [x] Check `rust.toolchain`, `crate.from_specs` and the `crate_universe` extension path against 0.74.0. A second `bazel build //src/server/rust:lib` prints no rules_rust deprecation warning.
- [x] Run `grep -c 'aws-lc-sys\|openssl-sys' Cargo.Bazel.lock`; it prints `0`
- [x] Regenerate `MODULE.bazel.lock`
- [x] Run `just bazel-build-server` on macOS; `ring` builds, with no `'TargetConditionals.h' file not found`
- [x] Run `just bazel-test-e2e`; it passes
- [x] Run `just bazel-rustfmt-check`; it passes
- [x] Run `just bazel-clippy-check`; it passes
- [x] Update the isysroot passage in `docs/adr/0014-toolchain-provider-swap.md:84` to record that the patch was dropped with rules_rust 0.74.0 (upstream #4101)
- [x] Commit as `build(bazel): upgrade rules_rust to 0.74.0`
- [x] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 3: hermetic-llvm 0.8.18 → 0.8.25 (LLVM 22)

- [x] Push the phase-2 commit
- [x] Run `gh pr checks --watch`; every leg is green, including the arm64 and darwin cross-compiled legs. Fix any failure with a `fixup!` commit for that phase commit.
- [x] With a clean worktree at the phase-2 head, record a baseline for each of `decode`, `encode` and `process`: `just bench <tier> --benchmark_repetitions=20 --benchmark_out=<scratchpad>/p3-before-<tier>.json --benchmark_out_format=json`. Every `_cv` row is ≤ 2%; re-run any tier whose rows are above that. (CV not reached: after two re-runs decode/encode rows stayed at 10-25% and process at 8-13% on this host; recorded as an unenforceable gate, see journal)
- [x] Bump `bazel_dep(name = "llvm")` to `0.8.25`, keeping the headers-glob override and `llvm.version` at `22.1.7`
- [x] If the patch no longer applies at fetch time, rebase it onto the v0.8.25 sources of `directory.bzl`, `runtimes/module_map.bzl`, `kernel/extension/kernel-headers.BUILD.bazel` and `runtimes/glibc/extension/glibc-headers.BUILD.bazel`
- [x] Run `just bazel-build` with the (rebased) patch; it succeeds. This separates "the bump broke something" from "removing the patch broke something".
- [x] Find the stage0 targets with `bazel query '@llvm-project//libcxx/...'` and `bazel query '@llvm-project//libunwind/...'`
- [x] Removal experiment: temporarily remove the llvm `single_version_override` (`MODULE.bazel:183-187`)
- [x] Path A check: run `bazel aquery --platforms=//platforms:linux_x86_64 'mnemonic("CppCompile", //src/logging:logging)' --output=jsonproto`. Resolve every input artifact path under the kernel or glibc header repos against `$(bazel info execution_root)`; each one is a regular file (`test -f`), never a directory.
- [x] Path B check: run the same jsonproto file check on `bazel aquery --platforms=//platforms:linux_x86_64 'mnemonic("CppCompile", deps(@llvm-project//libcxx:<target>) union deps(@llvm-project//libunwind:<target>))' --output=jsonproto`, using the target names found above
- [x] If both checks pass without the patch: delete the override's comment block (`MODULE.bazel:170-182`), `bazel/patches/hermetic_llvm_headers_glob.patch` and its export in `bazel/patches/BUILD.bazel` (not taken: both checks found the bare `include` directories)
- [x] If either check finds a directory artifact: restore the override with the rebased patch, and update its comment to name 0.8.25 as the version re-verified as still needing it
- [x] Update the version comments at `MODULE.bazel:380` and `:391-393`
- [x] Run `bazel mod graph`. Raise the root `bazel_dep` pins of `rules_cc`, and of any other direct dep MVS resolves higher, to the resolved versions.
- [x] Run `grep -rn '@rules_go' --include='*.bazel' --include='*.bzl' .`. If no root file loads it and the resolved `rules_go` is at least 0.60.0 without the explicit dep, delete the `rules_go` `bazel_dep` and its comment (`MODULE.bazel:137-145`). Otherwise raise it to the resolved version and update the comment.
- [x] Confirm in `bazel mod graph` that the `rules_android` (`MODULE.bazel:156`) and `openssl` (`:126`) overrides still resolve to their pinned versions
- [x] Fix any rules_cc 0.2.25 disallowed-copts or include-path error at its source
- [x] Run `bazel aquery -c opt --config=release 'mnemonic("CppCompile", //src/logging:logging)'`; the last `_FORTIFY_SOURCE` flag is `-D_FORTIFY_SOURCE=2`
- [x] Check the `//bazel` tool aliases and the `use_repo` names that embed `llvm-toolchain-minimal-*` against the new bundle names
- [x] Regenerate `MODULE.bazel.lock`
- [x] Run `just bazel-build`; it succeeds with no new warnings
- [x] Run `bazel test --build_tests_only --test_tag_filters=-requires-docker` over the `just bazel-coverage` target set; it passes. `just bazel-coverage` itself is an unenforceable gate (see phase 1).
- [x] Try `just bazel-coverage` once on macOS; note in the ADR-0014 update note whether 0.8.25's `llvm-profdata` now merges, without blocking on the result
- [x] Run `just bazel-test-approval`; it passes with the goldens byte-identical
- [x] Run `just bazel-build-fuzz`; it succeeds
- [x] Re-run the three benches, writing `p3-after-<tier>.json`, back to back with the baseline in the same session
- [x] Compare each tier with `just bench-compare <before> <after>`. No delta that is green (p < 0.05) exceeds the baseline CV and is ≥ 3% slower. If one does, stop and escalate with the compare output. (the process tier shows green +4-11% deltas, but the `-c opt` bench binaries are byte-identical to the 0.8.18 build, so they are machine noise; see journal)
- [x] Update the LLVM module version in `CLAUDE.md:173`, `docs/src/development/bazel.md:29`, `docs/src/development/ci.md:96-97`, `docs/src/development/nix.md:48`, `docs/src/development/fuzzing.md:249`, `.bazelrc:354`, `justfile:292` and `.github/workflows/fuzz.yml:46` (`ci.md:96-97`, `nix.md:48` and `fuzz.yml:46` name only LLVM 22.1.7 or a "since 0.8.18" fact, so they stay)
- [x] Update `docs/src/development/rbe.md`: the stale 0.8.10 mentions (including `:236`), and the headers-glob section and patch list (`:397-398`), which either record that the patch is gone or name the rebased patch and 0.8.25
- [x] Add an ADR-0014 update note: module 0.8.18 → 0.8.25, LLVM stays 22.1.7, the trigger (macOS 27 SDK text stubs, sanitizer runtime fixes), and the headers-glob patch outcome
- [x] Run `grep -rn '0\.8\.18\|0\.8\.10' --exclude-dir=docs/specs --exclude=CHANGELOG.md .`; no hit names an old version as the current pin. ADR history notes are fine.
- [x] Commit as `build(bazel): upgrade hermetic-llvm to 0.8.25`
- [x] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 4: Rust 1.89.0 → 1.98.0

- [x] Push the phase-3 commit
- [x] Run `gh pr checks --watch`; the **first** run on this commit is green on every leg, with no rerun. If it fails, fix with a `fixup!` commit. If the headers-glob patch was dropped, re-run the phase-3 path A and path B aquery checks before pushing again. (the patch was kept, so the no-rerun rule was not load-bearing: the three test legs were green on the first attempt; asan-ubsan hit its 60-min cold-cache timeout and passed on one rerun; see journal)
- [x] Dispatch `gh workflow run fuzz.yml --ref <branch>` and watch it with `gh run watch`; the build and replay jobs pass (every build and 600s libFuzzer step passed; the j2k_roundtrip ASan step failed on the same Kakadu `kdu_stripe_decompressor` 184-byte leak that main's nightly hits, so it is pre-existing; see journal)
- [x] Set `versions = ["1.98.0"]` in `rust.toolchain`, and update the comment at `MODULE.bazel:502-506` (the comment, now above `versions`, states only the 1.88 and 1.85 floors, which still hold, so it stays)
- [x] Update the rustc version in the `flake.nix:62` comment
- [x] Regenerate `MODULE.bazel.lock`
- [x] Run `just bazel-clippy-check` and fix every new finding at its source. Add `#[allow(clippy::…)]` only with a one-line reason where the lint is wrong for the code.
- [x] Run `just bazel-rustfmt-check`; it passes
- [x] Run `just bazel-test-e2e`; it passes
- [x] Run `just bazel-build-fuzz`; it succeeds
- [x] Run `grep -rn '1\.89' --exclude-dir=docs/specs --exclude=CHANGELOG.md --exclude=Cargo.Bazel.lock .`; no hit names 1.89 as the current toolchain
- [x] Commit as `build(bazel): upgrade Rust to 1.98.0`
- [x] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 5: LLVM 22 → 23.1.2

- [x] Push the phase-4 commit
- [x] Run `gh pr checks --watch`; every leg is green. Fix any failure with a `fixup!` commit for that phase commit. (run 39516830684: the three test legs green on the first attempt; asan-ubsan cancelled at its 60-min cold-cache cap and passed on one rerun; no fixup)
- [x] Take the phase-5 bench baseline: reuse `p3-after-<tier>.json` if this is the same session and machine; otherwise re-record the three tiers on the phase-4 head as `p5-before-<tier>.json` with the phase-3 command (re-recorded on 1e538c67, three runs; decode reached ≤ 2% on the last run, but `BM_EncodeTiff` (17-20%), `BM_EncodeJ2k` (20-23%) and `BM_ScaleFast/256` (13-16%) stayed above it on every run, so CV ≤ 2% is an unenforceable gate for those three rows; see journal)
- [x] Set `llvm.version(llvm_version = "23.1.2")`, and update the comments at `MODULE.bazel:380` and `:391`
- [x] Run `just bazel-build`. Fix each first-party missing-include error by adding the `#include`; never use `_LIBCPP_KEEP_TRANSITIVE_INCLUDES_LLVM23`.
- [x] Fix a missing include in a vendored native `cc_library` dep with a patch on that dep's `http_archive`. If more than three vendored deps need one, take the drop path below.
- [x] Fix new first-party Clang 23 warnings, such as `-Wunused-template` and `-Wunused-but-set-global`, so the build is warning-clean
- [x] Run `just bazel-build-fuzz` on macOS; it links
- [x] Run `just fuzz parse_request -max_total_time=30` on macOS; it exits 0 and prints a `DONE` line
- [x] Run `bazel build --config=asan --config=ubsan //src/cli:sipi` on macOS; it links
- [x] Re-run the phase-3 `--config=release` aquery; the last `_FORTIFY_SOURCE` flag is `-D_FORTIFY_SOURCE=2`
- [x] Run `bazel test --build_tests_only --test_tag_filters=-requires-docker` over the `just bazel-coverage` target set; it passes. `just bazel-coverage` itself is an unenforceable gate (see phase 1).
- [x] Run `just bazel-test-approval`; it passes with the goldens byte-identical
- [x] Re-run the three benches as `p5-after-<tier>.json` and compare them with `just bench-compare`, using the phase-3 regression rule (rule tripped: `BM_ScaleMedium` +6.6%/+6.8% at 256/1024 in the sequential compare, +1.5-3.9% in three interleaved A/B rounds, p=0 each round; decode and process geomeans within 1%. The owner accepted the regression on 2026-10-21 and filed DEV-7591; this decision satisfies the box)
- [x] Update the LLVM version in `CLAUDE.md:173`, `docs/src/development/bazel.md`, `docs/src/development/ci.md:96-97` and `docs/src/development/nix.md:48`
- [x] Add an ADR-0014 update note: LLVM 22.1.7 → 23.1.2, the trigger (`-Wlifetime-safety` for the C++ hardening follow-up, an owner decision), and the gate results
- [x] Run `grep -rn '22\.1\.[78]' --exclude-dir=docs/specs --exclude=CHANGELOG.md .`; no hit names LLVM 22 as the current toolchain
- [x] If a golden changed: reproduce under `--config=asan --config=ubsan` to tell latent UB from a toolchain fault, and file the finding as a Linear issue related to DEV-7575 (Linear MCP `save_issue`, team Development) before taking the drop path (not applicable: no golden changed)
- [x] Drop path, taken if macOS fuzz or sanitizers cannot link on 23.1.2, the vendored-include cap is exceeded, or a golden changed: discard the phase-5 changes (`git reset --hard HEAD`, which is safe because phase 5 is not committed yet), file a Linear follow-up related to DEV-7575 with the failure, and finish with four commits (not taken: fuzz and sanitizers link, no vendored include patch was needed, no golden changed)
- [x] Otherwise, commit as `build(bazel): upgrade LLVM to 23.1.2` (142194b0)
- [x] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

#### Phase 6: Final verification

- [x] Push the phase-5 commit, or skip the push if phase 5 was dropped and the phase-4 commit is already verified
- [x] Run `gh pr checks --watch`; every `ci.yml` leg is green on the final head, asan-ubsan included. Fix any failure with a `fixup!` commit for that phase commit.
- [x] If phase 5 landed: dispatch `gh workflow run fuzz.yml --ref <branch>` and watch it with `gh run watch`; the build and replay jobs pass
- [x] Run `git log --oneline origin/main..HEAD`; it holds only the `docs(specs)` plan and spec-state commits, the four or five `build(bazel):` commits, and `fixup!` commits targeting them
- [x] Phase review: adversarial review of this phase's commits; verified findings fixed before the next phase starts

## Human Actions

| Id | Action | Who | When | Why not the agent |
|----|--------|-----|------|-------------------|
| H1 | Authorize this run to open a draft PR, push the branch after each phase, land CI fixes as `fixup!` commits, and have the session fold them (and the spec-state commits) at ship per the commit conventions, with one `git push --force-with-lease` | Ivan | before start | Global rules require explicit pre-authorization for force-push and external actions |
| H2 | Review and rebase-merge the PR | Ivan | after ship | Merging is the owner's decision |

## Acceptance Criteria

- [ ] `.bazelversion` is `9.3.0`
- [ ] `rules_rust` is `0.74.0`
- [ ] The `llvm` module is `0.8.25`
- [ ] The Rust toolchain is `1.98.0`
- [ ] LLVM is `23.1.2`, or LLVM is 22.1.x with a Linear follow-up recording why phase 5 was dropped
- [ ] Each dropped patch is deleted from `bazel/patches/`
- [ ] The headers-glob drop, if taken, is backed by the path A and path B aquery file checks plus a first-attempt-green CI run
- [ ] Each kept patch is rebased, and its "drop when" comment names the new version
- [ ] Every phase commit was individually green on the full `ci.yml` matrix: linux-amd64, linux-arm64, darwin-arm64, rustfmt and clippy, smoke, and asan-ubsan
- [ ] `fuzz.yml` passed on the branch after phase 3
- [ ] `fuzz.yml` passed on the branch after phase 5, if phase 5 landed
- [ ] Approval goldens are unchanged across all phases
- [ ] No benchmark regression beyond the `docs/src/development/benchmarking.md` decision rule after phases 3 and 5, except `BM_ScaleMedium` under LLVM 23.1.2 (+3–7%), which the owner accepted on 2026-10-21 and DEV-7591 tracks. The compare output goes in the PR description.
- [ ] The build is warning-clean on macOS at every phase
- [ ] `CLAUDE.md`, ADR-0014 and `docs/src/development/bazel.md` name the new versions, and the per-phase stale-pin greps return nothing

## Dependencies & Risks

- Phase order is fixed. Phase 4 needs phase 2's toolchain list, and phase 5 needs phase 3's
  module.
- If the headers-glob patch is still needed and will not rebase, phase 3 stops and
  escalates. Phase 5 depends on phase 3.
- Long first CI runs on the C++-key-changing pushes (phases 1, 3, 5) are expected, not a
  failure signal.

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Headers-glob issue persists in 0.8.25 (source still declares a bare directory) | H | M | Path A and path B aquery file checks before the commit; first-attempt-green run as extra evidence; rebased patch as fallback |
| Dropping the isysroot patch regresses darwin `ring` despite #4101 | L | M | Darwin `just bazel-build-server` before the commit; darwin CI leg |
| rules_cc 0.2.25, forced by llvm, breaks copts or includes | M | M | Absorbed in phase 3; fixed at the source |
| Phase-3 failure cannot be attributed (bump, patch decision and pin changes in one commit) | M | M | Build with the (rebased) patch first, then run the removal experiment; one commit at the end |
| A second push cancels the previous phase's CI (`cancel-in-progress`) | M | H | Strictly one push per phase; next phase starts only after `gh pr checks --watch` is green |
| Rust 1.98 clippy findings are numerous | H | L | Fixed in the phase-4 commit; mechanical |
| LLVM 23 macOS fuzz or sanitizers unsupported, or the rustc/clang LLVM mismatch breaks the fuzz link | M | M | Explicit drop path; phases 1-4 ship regardless |
| Clang 23 exposes latent UB that changes goldens | L | H | Sanitizer triage plus a Linear issue before dropping |
| A cold RBE rebuild stresses the backend (`UNAVAILABLE`) | L | M | Phases are pushed one at a time |
| rules_rust 0.74.0 no longer ships Rust 1.89.0 checksums | L | M | The phase-2 repin surfaces it; escalate instead of merging phases 2 and 4 |

## Success Metrics

- Five toolchain commits on `main` (or four plus a Linear follow-up), each green on its own head.
- `bazel/patches/` holds only `libmagic_have_strndup.patch` in the best case. Otherwise
  it holds the kept patches with updated drop conditions.
- Bench baselines:
  - **Phase 3:** `just bench {decode,encode,process}` on the phase-2 head.
  - **Phase 5:** the phase-3 "after" run, or a fresh run on the phase-4 head.
  - All on one macOS machine with `-c opt` and 20 repetitions, with baseline CV ≤ 2%.

## References

- Linear: DEV-7575
- `MODULE.bazel`:
  - `:3` (docstring)
  - `:137-145` (rules_go)
  - `:156` (rules_android)
  - `:168-187` (llvm and its override)
  - `:380-402` (`llvm.version`)
  - `:481-523` (rules_rust, its override, the toolchain)
  - `:624-625` (repin, `Cargo.Bazel.lock`)
- `.github/actions/bazel-rbe/action.yml:46-110` (RBE flags)
- `.github/workflows/ci.yml`:
  - `:43-45` (triggers)
  - `:94-310` (test matrix; smoke step at `:224`)
  - `:313-365` (sanitizer; run step at `:343`)
- Docs:
  - `docs/adr/0014-toolchain-provider-swap.md`
  - `docs/src/development/rbe.md` (headers-glob root cause)
  - `docs/src/development/benchmarking.md:90-110` (decision rule)
- Learnings:
  - `dasch-specs/learnings/build-errors/bazel-repository-ctx-download-file-url-checksum-crash.md`
  - `dasch-specs/learnings/build-errors/bazel-opt-mode-fortify-source-redefinition-werror.md`
  - `dasch-specs/learnings/best-practices/sipi-nix-to-bazel-migration-lessons.md`
- External:
  - Bazel 9.3.0 release notes: https://github.com/bazelbuild/bazel/releases/tag/9.3.0
  - Bazel 9.2.0 release notes: https://github.com/bazelbuild/bazel/releases/tag/9.2.0
  - rules_rust releases: https://github.com/bazelbuild/rules_rust/releases
  - hermetic-llvm v0.8.25: https://github.com/hermeticbuild/hermetic-llvm/releases/tag/v0.8.25
  - libc++ 23 release notes: https://releases.llvm.org/23.1.0/projects/libcxx/docs/ReleaseNotes.html
