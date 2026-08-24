---
title: "SIPI: C++ microbenchmark suite + hermetic LLVM toolchain swap"
type: build
date: 2026-06-09
author: "Ivan Subotic"
status: reviewed(3)
linear: DEV-6613  # project: Sipi Performance Improvements
repositories:
  - sipi
adrs: []
future_adrs: [0014, 0015]  # 0014 toolchain provider swap (toolchains_llvm → hermetic-llvm); 0015 benchmark methodology + "no benchmark, no hot-path change" convention
---

# SIPI: C++ microbenchmark suite + hermetic LLVM toolchain swap

## Implementation Progress / Resume State (updated 2026-06-11)

> Read this first when resuming. It records what landed, decisions that
> override the body below, and the remaining task order. Inline sections are
> updated where they would otherwise mislead; this block is authoritative on
> deltas.

**PHASE 1 IS COMPLETE.** All six Phase-1 tasks landed, three-reviewer pass
(cpp / consistency / code-simplicity) clean after fixes, full test pyramid
green (39 passed, 1 skipped).

**Linear (added 2026-06-11):** this plan = **DEV-6613** (In Progress) in
project *Sipi Performance Improvements*
(https://linear.app/dasch/project/sipi-performance-improvements-799b5f11c60b —
description carries the current state + goals). Follow-ups filed from the
findings: **DEV-6614** TIFF pyramid reduced levels (High — first
optimization candidate) · **DEV-6615** lcms ICC-transform caching ·
**DEV-6616** SipiSize/SipiRotation parse cost · **DEV-6617** fixtures v2
(big_tree + Bellini) · **DEV-6618** HTJ2K Kakadu-license gate · **DEV-6619**
Lena fixture retirement.

The project was also consolidated (2026-06-11): the pre-existing
**DEV-6419** performance umbrella (Lua VM pooling DEV-6077, pixel-op churn
DEV-6078, cache locking DEV-6079, sendfile DEV-6080, event loop DEV-6081,
metadata skips DEV-6082, load-tier regression detection DEV-6103) and the
**DEV-6037** JP2→pyramidal-TIFF evaluation moved in from Sipi Maintenance.
DEV-6076 (SSL_CTX sharing) was canceled in favor of DEV-6035 (SSL removal
behind Traefik). DEV-6419's done-criterion now requires benchmark-gated
PRs per this plan's convention instead of "regression detector wired into
CI". Note for Phase-2+ sessions: DEV-6037 explicitly depends on DEV-6614 —
pyramidal-TIFF serving regresses downscales until SIPI uses reduced levels.

**Branch + PR:** Phase 1 = `perf/iiif-performance-analysis`, PR
**https://github.com/dasch-swiss/sipi/pull/665** (opened 2026-06-10, 16
commits — complete, awaiting review/merge). **DECISION (Ivan, 2026-06-11,
revised the same day): Phase 2 gets its OWN branch and PR** — the body's
original wholesale-revert rollback design stands unchanged. Branch off
`main` once PR #665 merges (preferred: the microbench suite must be in the
base to serve as the perf-regression gate), or off
`perf/iiif-performance-analysis` if starting before the merge. Rollback
unit = the Phase-2 PR reverted wholesale; `toolchains_llvm` stays default
on `main` until the swap is green on all three platforms.

**Commits landed (Phase 1, oldest first; `9c11518` docs predates this work):**
`0ad18d7` google_benchmark dev_dep · `89d21d7` Parse-tier bench · `50a8d8b`
vendored compare.py + rules_python pip hub · `5b7669b` _FORTIFY_SOURCE
undef-then-define · `2f4ab8b` just bench/bench-compare recipes · `633a3d8`
Process-tier bench · `b13f130` kakadu_archive→gh_release_archive (+ Bazel
9.1.0 file:// NPE fix) · `c20be88` @sipi_bench_fixtures archive + generator ·
`45ae850` SipiImage::write io.at() fix · `661af80` Decode+Encode-tier
benches · `d2b97de` benchmarking docs + conventions · `dd1b40d` clang-format ·
`040e11b` stale-reference sweep · `4e9a4fa` gh_release simplification ·
`74eb5c8` review fixes (measurement validity + positional-arguments).

**Where things live (orientation for the next session):**
- Recipes: `justfile` `bench <tier>` / `bench-compare` (note: `set
  positional-arguments` is enabled justfile-wide so filter regexes with `|`
  survive; recipes use `"${@:2}"`).
- Benchmarks: `src/iiifparser/parse_benchmark.cpp`, `src/process_benchmark.cpp`,
  `src/formats/{decode,encode}_benchmark.cpp`; targets in `src/BUILD.bazel`
  (manual + testonly); `**/*_benchmark.cpp` glob-excluded from `:sipi_lib`.
- Fixtures: `@sipi_bench_fixtures` ← `bazel/benchmark_fixtures_extension.bzl`
  (tag/asset/sha256 pins) ← `bazel/gh_release.bzl` (shared with Kakadu) ←
  release `sipi-bench-fixtures-v1` on dsp-ci-assets; generator + provenance:
  `tools/benchmark/generate_fixtures.sh`.
- compare.py: vendored under `tools/benchmark/` (pinned to google_benchmark
  1.9.5; re-sync when bumping), hermetic numpy/scipy via `sipi_bench_pip`.
- Docs: `docs/src/development/benchmarking.md` is the single source of truth
  for workflow + regression decision rule; CLAUDE.md and REVIEW.md carry the
  convention text.

**Decisions made this session (override the plan body where noted):**

1. **Benchmark BUILD location → `src/BUILD.bazel`, not new sub-package BUILD
   files.** Creating `src/formats/BUILD.bazel` / `src/iiifparser/BUILD.bazel`
   *today* would carve `src/formats/*.cpp` (SipiIOTiff etc.) out of the
   `//src:sipi_lib` `**/*.cpp` glob (globs stop at package boundaries) and break
   the production library. So the benchmark `cc_binary` targets are declared in
   the existing `//src` package's `src/BUILD.bazel`; sources live physically
   under `src/formats/`, `src/`, `src/iiifparser/` and are referenced by path.
   They still move with their module when ADR-0003 promotes it — only the BUILD
   file is `src/BUILD.bazel` until then. **Supersedes the File-Manifest line
   "create `src/formats/BUILD.bazel`".**

2. **Benchmark targets are `testonly = True`** — they (will) dep the `testonly`
   `//test:test_paths`; testonly propagates. Correct for dev-only tools.

3. **`bench-compare` mechanism = rules_python + own pip hub** (decided by Ivan,
   2026-06-10). The upstream `@google_benchmark//tools:compare` py_binary is
   *unconsumable*: google_benchmark declares both `rules_python` and its
   `tools_pip_deps` pip hub as `dev_dependency = True`, which Bzlmod drops for
   non-root modules, so `@tools_pip_deps` never exists in our graph. Plan:
   - `bazel_dep(name = "rules_python", version = "1.7.0")` — **already resolved
     transitively at 1.7.0** (googletest→re2→pybind11, protobuf, rules_foreign_cc),
     so zero MVS churn.
   - a dev pip hub `sipi_bench_pip` (`numpy == 2.4.1`, `scipy == 1.17.0`,
     mirroring google_benchmark 1.9.5's own `tools/requirements.txt`) + a 3.12
     python toolchain.
   - **vendor** `compare.py` + `gbench/{__init__,report,util}.py` under
     `tools/benchmark/` pinned to 1.9.5 (copy from the fetched
     `external/google_benchmark+/tools/`). `py_binary //tools/benchmark:compare`,
     `deps = [requirement("numpy"), requirement("scipy")]`, `imports = ["."]`.
   - `just bench-compare before after` → `bazel run //tools/benchmark:compare --
     benchmarks before after`.
   - **`flake.nix` python3/scipy is NO LONGER needed** — the pip hub provides
     them hermetically. **The original `flake.nix` task is dropped.**

4. **Fixture source = imagecompression.info (primary) + Bellini Commons (giant
   master)** — **supersedes the Met/Cleveland museum-API approach** in
   Dependencies. Rationale (best-practices research, 2026-06-10): a live museum
   API is not byte-stable (can't sha256-pin) and serves *lossy* JPEGs, baking
   JPEG artifacts into every "lossless" variant and distorting codec-cost
   numbers.
   - Primary: imagecompression.info "New Test Images" `big_building`
     (7216×5412) / `big_tree` (6088×4550) — 16-bit, lossless PPM, the de-facto
     codec-benchmark corpus (libjpeg-turbo vendors from it). **Must ship the
     upstream notice file in the tarball** ("no sale" clause; not CC0).
   - Giant master (for a dramatic ≥100× pyramid demo): Bellini "Saint Francis
     in the Desert", 30,000×26,319, PD, Wikimedia Commons (Google Art Project) —
     the exact fixture libvips uses. Caveat: JPEG-sourced (re-encode artifacts);
     minor PD-Art EU nuance (fine for a Swiss-hosted OSS repo).
   - Process-tier shaped sources: self-generate from CC0 ICC profiles
     (Little-CMS / Elle Stone) + a crop of a chosen master, deterministic under
     `SOURCE_DATE_EPOCH=946684800`. **Existing repo fixtures suffice short-term:**
     alpha → `knora/Leaves-small-alpha.tif`; 16-bps → `knora/png_16bit.tif`/`.png`;
     embedded-ICC → `jpeg/cmyk/cmyk_photoshop_app14.jpg`; known-dims →
     `knora/Leaves8.tif`. Keep the ISO 15444-4 set as the JP2 conformance corpus.

5. **Side finding (separate cleanup, NOT in this plan):** retire the Lena
   fixtures (`unit/lena512.*`, `knora/lena512.jp2`) — USC-SIPI research-only
   license + ethics. They are wired into existing unit tests, so a scoped
   follow-up, not a benchmark task.

**Empirical finding (the suite already earns its keep):** the Parse-tier bench
shows `SipiSize`/`SipiRotation` parse far slower than `SipiQualityFormat` —
under `-c opt` (2026-06-10): 761 ns / 464 ns vs 9.5 ns ≈ **80×** (likely
exception/stream-based parsing). Candidate before/after target.

**Next-session work (in order):**
1. **Phase-1 leftovers (small):**
   - Run the bench suite once on **linux-x86_64** (acceptance criterion;
     OrbStack/colima or a Linux box — the suite is local-dev-only, CI never
     runs it).
   - A formal **20-repetition CV ≤ 2% baseline** on a quiesced machine
     (`just bench <tier> --benchmark_repetitions=20 …`); record per-tier
     baselines (this session's machine was build-loaded throughout).
2. **Phase 2 — hermetic LLVM toolchain swap**, on its own branch + PR (see
   decision above; ideally branched off `main` after #665 merges). Task
   list unchanged in the body below: MODULE.bazel toolchain-block rewrite
   first, then fuzz-toolchain fold, glibc-shim deletion, coverage
   re-pointing, the foreign_cc per-dep matrix, and the **pre-swap
   microbench baseline on a fixed host** (now possible — that was the
   point of flipping the phases).
3. **Out-of-plan follow-ups noted, not started:** TIFF pyramid-level
   exploitation (finding #2 below — first real optimization candidate, do it
   as its own benchmark-gated PR after this one); Lena fixture retirement
   (decision 5); big_tree + Bellini fixture release v2; HTJ2K revisit if the
   Kakadu license ever covers HT.

**Open decision for next session:** ~~http_archive hosting location for the
fixture tarball (GCS bucket vs GitHub release)~~ **RESOLVED 2026-06-10
(Ivan): dsp-ci-assets release, reusing the kakadu fetch pattern.** Landed as
`gh_release_archive` (kakadu.bzl generalized into bazel/gh_release.bzl) +
`@sipi_bench_fixtures` (tag `sipi-bench-fixtures-v1`, 321 MB tar.zst,
big_building matrix only per Ivan's v1-scope decision; big_tree + Bellini
deferred to a later release).

**Session 2026-06-10 findings (all tiers landed and running):**
1. **Slow-baseline criterion met:** full-res 256px tile from plain JPEG =
   219 ms vs 0.96 ms from tiled-pyramid TIFF ≈ **228×** (-c opt,
   darwin-aarch64).
2. **SIPI does not exploit TIFF pyramid levels:** `!256,256` thumbnail from
   pyr-none.tif = 603 ms (≈ full 39 Mpx decode + downscale) vs 35 ms from
   JP2 (Kakadu uses reduction levels). Prime suspect for Pillay's "SIPI
   slowest on TIFF"; first optimization candidate.
3. **ScaleHigh ≈ 89× ScaleFast** at 1024px and nearly target-size-
   independent; **CMYK→sRGB convertToIcc ≈ 16 ms on 128×128** (lcms
   transform-creation dominated, per-request).
4. **HTJ2K dropped from the matrix:** Kakadu HT block coder is
   license-gated (FBC_ENABLED commented out in our v8.7 pin) — production
   SIPI can neither encode nor decode HT codestreams.
5. **Latent bugs fixed en route:** (a) `.bazelrc` hardening `-D_FORTIFY_SOURCE=2`
   collided with Bazel's opt-feature `=1` → -Werror failure in
   google_benchmark; fixed with undef-then-define. (b) `ctx.download(file://…)`
   sha256 trick NPE-crashes Bazel 9.1.0 → gh_release_archive verifies via
   shasum. (c) `SipiImage::write` docstring advertised ftype "j2k" but the
   handler map key is "jpx"; `io[ftype]` null-inserted and segfaulted →
   docstring fixed, dispatch now `io.at(ftype)`. (d) WebP is not an
   encode format (TIFF-internal compression only) — encode matrix is
   jpg75/jpg90/png/tif/jpx.

**Phase 2:** task list AND rollback design unchanged from the body below —
own branch, own PR, wholesale revert (the brief same-day "same branch/PR"
decision was reversed within hours; the body is authoritative again).
Gated on the Phase-1 microbench as the perf-regression gate: record the
pre-swap baseline (`--benchmark_repetitions=20`, all four tiers, quiesced
fixed host, JSON files kept) BEFORE touching MODULE.bazel.

## Phase 2 — Resume State (updated 2026-06-16)

> Read this FIRST. Supersedes the 2026-06-15 block below on all deltas.
> **darwin is fully GREEN; Linux is committed but UNVALIDATED (needs CI).**

**Branch:** `build/dev-6613-hermetic-llvm-toolchain`, **force-pushed 2026-06-16**
(the WIP commit was amended). 5 commits, newest last:
- `740448b` build: WIP swap toolchains_llvm → hermetic-llvm 0.8.8 (LLVM 22.1.7)
  — **now includes the link-wrapper fix** (amended in).
- `267bffd` build(bazel): bump platforms 1.0.0→1.1.0 + rules_cc 0.2.18→0.2.19 to
  MVS versions + `bazel mod tidy`.
- `3d961b4` docs(build): fuzz-toolchain comment cleanup (single-toolchain fold).
- `bcb5152` build(bazel): repoint debug-split + coverage to the LLVM 22 bundle.
- `bbbfa35` ci: disable sanitizer/coverage/fuzz auto-triggers under hermetic-llvm.

No PR opened yet (next session: open it to trigger `ci.yml`).

**OPEN BLOCKER from 2026-06-15 — RESOLVED.** The C++ `link-wrapper` failure:
hermetic-llvm 0.8.8's macOS "complete" toolchain routes link actions through a
`link-wrapper` binary that `execv`s the compiler named by `LLVM_CLANGXX`, which
the toolchain sets to an *execroot-relative* path. foreign_cc relocates each
build and runs cmake/make from a different cwd, so the relative path fails
("No such file or directory"). CMake's compiler-detection links a C++ test exe,
so every C++ foreign_cc dep (exiv2, sentry, **tiff** — libtiffxx) broke; C-only
deps and kakadu (its own `clang++`-from-PATH wrapper) are unaffected.
**Fix:** re-export `LLVM_CLANGXX` as an absolute path
(`$$EXT_BUILD_ROOT$$/$(execpath //bazel:clang++)`) via the foreign_cc rule
`env` (foreign_cc merges rule env *after* the toolchain env). New helpers
`darwin_link_wrapper_env()` / `darwin_link_wrapper_build_data()` in
`bazel/foreign_cc_helpers.bzl`, new `//bazel:clang++` alias, wired into
exiv2/sentry/tiff.

**Validated on darwin-aarch64 this session:** `just bazel-build` GREEN
(`sipi 5.0.1` runs); `just bazel-coverage` runs the full pyramid **39/40 pass
(1 skipped)** — matching the Phase-1 baseline — and **the approval goldens pass
UNCHANGED under LLVM 22** (the central equivalence-gate criterion; no codec/ICC
byte drift from the 19→22 swap).

**MAJOR FINDING — 3 instrumentation CI gates are broken under hermetic-llvm
0.8.8** (toolchain-level, not recipe bugs):
1. **coverage** — `bazel coverage` yields an empty lcov (LF=0). The toolchain's
   `llvm_coverage_map_format` feature does not activate (no `-fcoverage-mapping`
   on compiles even with explicit flags); it is gated on the **unmerged
   rules_cc PR #385** (per the toolchain's own TODO).
2. **sanitizer (asan/ubsan)** — the minimal prebuilt ships NO sanitizer runtime;
   the only build mechanism (`--@llvm//config:asan=true`) instruments the whole
   toolchain GLOBALLY, adding `-fsanitize=*` to every compile incl. foreign_cc
   deps (jansson's CMake compiler-detection link then breaks). Incompatible with
   SIPI's `--per_file_copt` scoping. Building the runtime archives standalone
   also fails (`<cassert>` not found — only compile inside the runtime config).
3. **fuzz (libFuzzer)** — no fuzzer runtime; same global-instrument problem.

**DECISION (Ivan, 2026-06-16): PROCEED with the swap + CARVE OUT the 3 gates.**
Land the swap for its core wins (hermeticity, no Chromium sysroots, darwin SDK
from CDN, cross-compile path); the core gate passes the equivalence test.
Carved out (`bbbfa35`): `sanitizer.yml` (was a PR merge gate), `coverage.yml`
(post-merge on main), `fuzz.yml` (nightly) — all auto-triggers disabled,
`workflow_dispatch` kept, each with a comment pointing at ADR-0014.

**Ivan's diagnostic question (answered):** "if all deps were BCR, would it
work — is foreign_cc the problem?" → foreign_cc is the *proximate* cause of the
configure-time link break (BCR `cc_library` deps have no compiler-detection
probe), so an all-BCR graph dodges failure mode #1 on Linux. But SIPI **cannot**
be all-BCR — **Kakadu is proprietary/license-gated, never on BCR** — and the
darwin runtime-naming gap (`libclang_rt.asan_osx_dynamic.dylib`) is independent
of foreign_cc. So "all BCR" helps but isn't a complete fix.

**Other decisions/notes this session:**
- `bazel mod tidy` STRIPS `use_repo(llvm_toolchain_minimal, …)` — it can't see
  the bundle referenced from `//bazel`'s `alias(actual = "@…")` string labels.
  These use_repos are **load-bearing** (the //bazel aliases) and were re-added
  with a comment. The resulting "reported incorrect imports" warning is benign
  + pre-existing. Re-add after any future `bazel mod tidy`.
- **glibc23_compat.c KEPT** (task-7 deletion deferred). The shim defines
  `__isoc23_*`/`fcntl64` as plain wrappers forwarding to canonical glibc entries
  present in any modern glibc (incl. the bundled ~2.28), so it should still
  resolve under hermetic-llvm. Kakadu still compiles via Nix clang (its postfix
  greps for the now-deleted Chromium sysroot → `SYSROOT_FLAG=""`, host glibc
  headers, `__isoc23_*` symbols → shim handles them). Deletion + the clean
  kakadu-reroute-through-hermetic-clang is a hermeticity *cleanup*, not required
  for a green core build — filed as a follow-up.
- debug-split + coverage repoint: `//bazel:{llvm-objcopy,llvm-readelf,llvm-cov,
  llvm-profdata}` aliases select by exec arch (linux-amd64/linux-arm64 bundles +
  darwin default); `use_repo`'d both linux bundles. Coverage tools MUST be the
  LLVM-22 bundle binaries (a host LLVM-19 profdata can't read a v22 `.profraw`).

**Remaining Phase-2 work (next session, in order):**
1. **Open the Phase-2 PR** (off `build/dev-6613-…`) to trigger `ci.yml`; **drive
   the CORE gate green on all 3 platforms** — Linux is UNVALIDATED locally
   (kakadu/glibc shim, debug-split/coverage repoint, foreign_cc C++ deps under
   the hermetic toolchain all need CI confirmation). Iterate on Linux failures.
2. **Write ADR-0014** (NOT yet written) — toolchain provider swap: 19→22 reality,
   constraint-model rewrite, hermetic-`llvm-ar` + preinstalled foreign_cc tools,
   the **link-wrapper/clang++ finding**, the **3 carved-out instrumentation
   gates** + re-arm conditions, macOS-SDK-from-CDN (403 → web.archive mirror)
   caveat, the v0.8.x exact-pin policy. Several CI-comment + this-plan refs point
   to it.
3. **docs**: `docs/src/development/{bazel,building}.md` toolchain sections;
   `sipi/CLAUDE.md` stale "LLVM 19 via toolchains_llvm" line (now 22/hermetic).
4. **File Linear follow-ups** (Ivan: "file follow-ups"): coverage under
   hermetic-llvm (rules_cc #385); sanitizer runtime provisioning; libFuzzer
   runtime provisioning; glibc23_compat.c deletion + kakadu reroute through the
   hermetic clang (task 7).
5. `--dynamic_mode=off` re-validate on linux-aarch64; foreign_cc per-dep
   symbol-version matrix (body tasks) — via CI.
6. Microbench before/after the swap on a fixed host (still deferred).
7. `toolchain.exec(linux-aarch64)` is NOT declared — if CI builds linux-arm64
   natively, add it or native arm64 toolchain resolution fails (2026-06-15 note).

---

## Phase 2 — Resume State (updated 2026-06-15)

> Read this FIRST when resuming Phase 2. It records the decisions made this
> session (which override the Phase-2 body below where noted), what landed,
> what's validated, the open blocker to resume from, and the remaining order.
> Phase-2 work is on its own branch, **WIP-committed but RED** (the swap is
> mid-integration; this is expected). Ivan deferred recording the formal
> pre-swap microbench baseline this session (chose "straight to the rewrite").

**Branch:** `build/dev-6613-hermetic-llvm-toolchain` (off `main`, after PR #665
merged 2026-06-15). No PR yet. WIP commit only; not pushed.

**Decisions made this session (override the Phase-2 body where noted):**

1. **`llvm` BCR module pinned at `0.8.8`, not `0.8.6`** (Ivan: "use the
   newest"). 0.8.8 is API-identical to 0.8.6 for our use: same deps
   (`platforms` 1.1.0, `rules_cc` 0.2.19, `rules_python` 1.7.0, `bazel_skylib`
   1.8.2, …), same default LLVM 22.1.7, same prebuilt set, same MacOSX26.4 SDK
   pin. 0.8.8 adds ThinLTO-on-all-platforms, test path-mapping, an ABI-checks
   debug knob. `bazel_compatibility >= 7.7.0` (we're on Bazel 9.1.0).

2. **LLVM `22.1.7`, NOT `19.1.7`** (Ivan's decision; **supersedes the body's
   `19.1.7` pin and the MODULE.bazel snippet**). 19.1.7 has **no prebuilt** in
   hermetic-llvm 0.8.x — the prebuilt registry (`llvm_versions.json`) is
   `21.1.8` + `22.1.0`–`22.1.7`, default `22.1.7`. Pinning 19.1.7 forces
   `--@llvm//toolchain:source=bootstrapped` = a multi-hour from-source clang
   build (kills the CI inner loop + Cloud Run cache). So the provider swap
   **necessarily carries a 19→22 compiler bump**. Chosen approach: ONE PR
   (swap + bump together), lean on the equivalence gate (approval goldens must
   pass unchanged); bisect provider-vs-compiler if goldens drift.

3. **glibc floor = the module default (~2.28), not explicitly pinned.** More
   portable at runtime than the prior Chromium-bullseye 2.31 (a 2.28-built
   binary runs on any glibc ≥ 2.28). Pin `@llvm//constraints/libc:gnu.2.31` on
   the target platforms ONLY if a Linux build surfaces a dep needing a
   2.29–2.31 symbol. This makes the **glibc23_compat.c shim deletion** cleaner
   (the hermetic per-version glibc stubs eliminate the `__isoc23_*`/`fcntl64`
   class for Bazel-driven compiles), but the shim deletion is still coupled to
   **re-routing Kakadu's foreign_cc compile through the hermetic clang** (today
   Kakadu uses Nix clang + a `--sysroot` at the Chromium sysroot — see
   `bazel/kakadu.BUILD.bazel:107-162`); that is Linux/CI work (task §"Phase 2"
   item 4).

4. **macOS SDK** = MacOSX26.4 fetched hermetically from Apple's CDN by the
   module's `osx.from_archive`. **Finding (CI fragility):** the primary
   `swcdn.apple.com/.../CLTools_macOSNMOS_SDK.pkg` URL returns **403 Forbidden**
   (Apple rotates these); the build only succeeded because the module ships a
   `web.archive.org` mirror fallback. Consider caching the SDK pkg through the
   bazel-remote Cloud Run proxy (or a GCS object) for CI resilience. Document
   in ADR-0014.

5. **foreign_cc archiver → hermetic `llvm-ar`** (Ivan chose this over the host
   `/usr/bin/ar` the old `ar_wrapper.sh` used). hermetic-llvm's macOS archiver
   is `llvm-libtool-darwin` (Apple-style: `archiver_flags = ["-D",
   "-no_warning_for_no_symbols", "-static"]`, needs `-static -o`), which
   foreign_cc's cmake/autotools deps invoke with GNU-`ar` conventions and
   reject (`-static option: must be specified` / `-o option: must be
   specified`). Routed to the bundle's GNU `llvm-ar` via new `//bazel:llvm-ar`
   + `//bazel:llvm-ranlib` aliases (→ `@llvm-toolchain-minimal-22.1.7-darwin-
   arm64//:bin/llvm-ar` / `:bin/llvm-ranlib`), a reworked `bazel/ar_wrapper.sh`
   (forwards to `$SIPI_AR`, strips the libtool flags) for autotools deps, and
   new `darwin_cmake_cache_entries()` / `darwin_cmake_build_data()` helpers
   (set `CMAKE_AR`/`CMAKE_RANLIB`) for cmake deps. **Darwin-only** — on Linux
   the toolchain's default archiver is already GNU `llvm-ar`.

6. **foreign_cc build tools → Nix-preinstalled** (registered
   `@rules_foreign_cc//toolchains:preinstalled_{make,pkgconfig,cmake,ninja}_toolchain`).
   The from-source bootstraps (`BootstrapGNUMake`, `BootstrapPkgConfig`) fail
   under the hermetic archiver (gnulib's `build.sh` / pkg-config's bundled glib
   use GNU-`ar` syntax against `llvm-libtool-darwin`). Consistent with the
   plan's stance that foreign_cc build tools stay Nix-provided.

7. **Fuzz toolchain folded** into the single `@llvm_toolchains//:all`. The old
   second `llvm_toolchain_fuzz` is gone; the extra `//tools/fuzz:fuzz_enabled`
   platform constraint is ignored by toolchain resolution (Bazel subset-
   inclusion rule — confirmed). The stale `tools/fuzz/BUILD.bazel` and
   `bazel/platforms/BUILD.bazel` comments referencing `@llvm_toolchain_fuzz`
   still need a cleanup pass (task §"remaining" item 8).

8. **The prebuilt bundle exposes per-tool targets** (`:llvm-ar`, `:llvm-cov`,
   `:llvm-profdata`, `:llvm-objcopy`, `:llvm-strip`) **AND raw bin files**
   (`:bin/llvm-ranlib`, `:bin/llvm-readelf`, `:bin/llvm-readobj`, …). Reference
   the raw `@<bundle>//:bin/<tool>` files (covers ranlib/readelf, which have no
   `_cc_tool` wrapper). Repo name embeds version + exec triple
   (`llvm-toolchain-minimal-22.1.7-<os>-<arch>`) — version-fragile; centralize
   behind `//bazel` aliases (only `darwin-arm64` is use_repo'd so far; add
   `linux-amd64` + `linux-arm64` when wiring coverage / the objcopy genrule).

**Validated on darwin-aarch64 this session:** MODULE.bazel resolves; LLVM
22.1.7 prebuilt + MacOSX26.4 SDK fetch; first-party C++23/libc++ compiles;
analysis configures all ~38k targets; **C foreign_cc deps build** (png,
jansson, webp — proving the hermetic-`llvm-ar` cmake fix).

**OPEN BLOCKER — resume here.** C++ foreign_cc deps (`ext/exiv2` first, then
`ext/sentry`) fail at CMake compiler detection:
`link-wrapper: failed to execute .../llvm-toolchain-minimal-22.1.7-darwin-
arm64/bin/clang++: No such file or directory`. hermetic-llvm wraps the linker
in a `link-wrapper` binary (ThinLTO, added in 0.8.8) that can't resolve
`clang++` inside rules_foreign_cc's relocated build sandbox when a dep **links**
a C++ program (CMake's compiler-detection test exe). C deps only archive →
pass; C++ deps link → fail. **Next investigation:** how foreign_cc stages the
hermetic toolchain's compiler + link-wrapper + the clang++ symlink; options —
disable the ThinLTO link-wrapper (a `--@llvm//...` flag?), add the full
toolchain to the cmake rule's `build_data`, or a path-resolution fix. Likely a
staging/config gap (hermetic-llvm advertises foreign_cc support), not
fundamental. Check the exiv2 `CMake.log` under `bazel-out/.../ext/exiv2/
exiv2_foreign_cc/CMake.log`.

**Files changed this session (in the WIP commit):**
- `MODULE.bazel` — `bazel_dep(llvm, 0.8.8)` replaces `toolchains_llvm`;
  `llvm_source.version(22.1.7)` + `toolchain.exec/target` block (Chromium
  sysroots + fuzz toolchain deleted); `register_toolchains(@rules_foreign_cc//
  toolchains:preinstalled_{make,pkgconfig,cmake,ninja}_toolchain)`; `use_repo`
  of the `@llvm-toolchain-minimal-22.1.7-darwin-arm64` bundle.
- `.bazelrc` — macOS SDK hacks deleted (`-nostdinc++`/`-isystem MacOSX.sdk`,
  `DEVELOPER_DIR`/`SDKROOT`); kept `build:macos --repo_env=PATH` + the
  `-mmacosx-version-min=13.3` floor; updated the `BAZEL_DO_NOT_DETECT_CPP_
  TOOLCHAIN` comment (flag kept, test-remove later).
- `bazel/BUILD.bazel` — `llvm-ar` / `llvm-ranlib` aliases.
- `bazel/ar_wrapper.sh` — forwards to hermetic `$SIPI_AR` (was `/usr/bin/ar`).
- `bazel/foreign_cc_helpers.bzl` — `SIPI_AR` in `darwin_autotools_env` +
  `llvm-ar` in its build_data; new `darwin_cmake_cache_entries()` /
  `darwin_cmake_build_data()`.
- `ext/{png,jansson,exiv2,webp,sentry,tiff}/BUILD.bazel` — cmake deps wired to
  the cmake AR helpers.

**Deferred cleanups (do before the PR):** run `bazel mod tidy` (fixes the
`use_repo` warnings: `llvm_source`'s `llvm-raw`/`llvm_config`/`llvm_zlib`/
`llvm_zstd`, and the `llvm_toolchain_minimal` indirect-import warning); bump the
direct-dep pins `platforms` 1.0.0→1.1.0 and `rules_cc` 0.2.18→0.2.19 to match
the MVS-resolved versions (silences `--check_direct_dependencies` warnings).

**Exec-platform caveat for next session:** declared `toolchain.exec(macos-
aarch64)` + `toolchain.exec(linux-x86_64)` but NOT `toolchain.exec(linux-
aarch64)`. If CI builds linux-arm64 **natively** (on an arm64 runner), add
`toolchain.exec(arch = "aarch64", os = "linux")` or native arm64 toolchain
resolution fails. Verify native-vs-cross for arm64 in the CI/docker recipes.

**Remaining Phase-2 work, in order:**
1. Fix the C++ `link-wrapper` blocker; finish darwin foreign_cc (exiv2, sentry,
   verify jbigkit + kakadu darwin); get `just bazel-build` green on darwin.
2. `//src:sipi_debug_split` genrule (`src/BUILD.bazel:600-633`) — repoint
   `@llvm_toolchain_llvm//:objcopy`/`:readelf` → the bundle's
   `:bin/llvm-objcopy` + `:bin/llvm-readelf` (Linux-only; add the linux bundles
   to `use_repo` + `//bazel` aliases). NOTE: Bazel resolves repo mappings
   lazily, so this dangling ref does NOT break the darwin `//src/cli:sipi`
   build, but DOES break Linux / `bazel build //...`.
3. Linux validation (CI or OrbStack): glibc23_compat.c deletion + Kakadu
   foreign_cc re-routing through the hermetic clang (body task); foreign_cc
   per-dep symbol-version matrix (body task); `--dynamic_mode=off` on aarch64.
4. Coverage repoint — `just bazel-coverage` `llvm-cov`/`llvm-profdata` → the
   linux bundle's `:bin/llvm-cov` + `:bin/llvm-profdata` (body task).
5. libFuzzer validation after the fuzz fold (body task); fix stale
   `@llvm_toolchain_fuzz` comments in `tools/fuzz/` + `bazel/platforms/`.
6. Microbench before/after the swap on a fixed host (body task).
7. ADR-0014 + `docs/src/development/{bazel,building}.md` (body task) — fold in
   the 19→22 reality, the SDK-CDN-mirror finding, the hermetic-`llvm-ar` +
   preinstalled-build-tools decisions, and the v0.8.x pinning policy.
8. `bazel mod tidy` + the direct-dep version bumps (above).
9. 3-platform CI equivalence gate (approval goldens unchanged, coverage,
   sanitizer + fuzz, Docker both arches).

## Overview

Two-phase, single-plan initiative driven by Ruven Pillay's *Evaluating IIIF Server Performance* (IIIF Annual Conference 2026), which benchmarked SIPI as the **slowest server tested for TIFF and JPEG** (≈13× iipsrv on JPEG end-to-end, ≈9× on JPEG-encoded TIFF), while competitive on JPEG2000.

The real deliverable is a **mindset shift**, not a one-off measurement: *no benchmark, no hot-path change*. Before any change to an image decode/encode hot path, a microbenchmark must exist; the change is justified with a before/after comparison.

- **Phase 1 — C++ microbenchmark package** (Google Benchmark). Fast, local, in-process decode/encode benchmarks on the **current** toolchain. Establishes the discipline and the tooling.
- **Phase 2 — Hermetic LLVM toolchain swap** (`toolchains_llvm` → the BCR `llvm` module = `hermeticbuild/hermetic-llvm`). Fully hermetic toolchain that bundles Linux sysroots and fetches the macOS SDK from Apple's CDN, decoupling the build from host Nix/Xcode-CLT and unlocking darwin→linux cross-compilation.

**Sequencing rationale (decided 2026-06-09):** the originally-requested order (toolchain → benchmark) was **flipped** after spec review. Phase 1 is a `dev_dependency` that builds under the current toolchain with near-zero blast radius and delivers the discipline immediately. Crucially, it then becomes the **perf-regression gate for Phase 2** — a toolchain swap changes codegen, and without the microbench suite the swap would ship with no way to detect a decode/encode perf regression.

The end-to-end HTTP load layer from the PDF is **explicitly deferred** (apples-to-oranges across machines/OSes; the mindset shift lives in the microbench loop, not in CI load tests).

## Problem Statement

1. **No perf measurement capability.** SIPI has zero benchmarking infrastructure. The only timing assertions are gross smoke thresholds in `test/e2e-rust/tests/latency.rs`. There is no way to answer "did this change to the TIFF decode path make it faster or slower," so hot-path edits ship blind. `docs/src/development/testing-strategy.md:718-735,913` lists component microbenchmarks as aspirational ("post-Rust-migration, criterion") — but the codec is C++ today and needs measuring now.

2. **An external benchmark says we are slow** at exactly the formats most of our pipeline touches (TIFF, JPEG). The result is from the maintainer of a competing server (iipsrv), single-threaded localhost latency on a 2010-era Xeon, against SIPI 4.1.1 (we are on 5.0.1). None of that invalidates it; it warrants our own reproducible measurement before drawing conclusions or optimizing. Phase 1 measures *intra-SIPI* per-format and per-stage cost — what we can measure rigorously and locally; the cross-server, end-to-end comparison the PDF ran is deferred (see Out of scope).

3. **The build is coupled to host Nix/Xcode tooling.** `toolchains_llvm` 1.7.0 has no bundled macOS sysroot, forcing a stack of `.bazelrc` workarounds (`-nostdinc++`, `-isystem .../MacOSX.sdk`, `SDKROOT`, `DEVELOPER_DIR`) and manual Chromium debian-sysroot tarball pins for Linux. A Mac cannot build the Linux Docker image locally (no working darwin→linux cross-compile), so the inner loop for container work requires CI or a Linux box.

## Proposed Solution

### Phase 1 — C++ microbenchmark package (Google Benchmark)

Add `google_benchmark` as a `dev_dependency` and CI-excluded `cc_binary` benchmarks that time the production codec in-process via the `SipiImage` facade, **co-located with the source module per ADR-0003** (not in `test/`). Adapt the PDF's format matrix. Wire a `just bench` recipe and a documented before/after `compare.py` workflow. Codify the discipline in `CLAUDE.md` and `REVIEW.md`.

### Phase 2 — Hermetic LLVM toolchain swap

Replace the `toolchains_llvm` toolchain block with the BCR `llvm` module's constraint-based model. Delete the Chromium-sysroot repo rules and the macOS SDK `.bazelrc` hacks. Re-prove equivalence on all three platforms — the existing approval goldens must pass **unchanged** (no codec/ICC byte drift from the swap; `test/approval/BUILD.bazel` is not platform-gated, so a single shared golden set is checked identically on each platform) — using the Phase-1 microbench as the perf-regression gate. Record the decision as **ADR-0014**.

## Technical Approach

### Architecture

#### Phase 1 — what we measure and how

**Codec entry point (corrected against the source).** There is **no** `SipiImage::read(region, size)` convenience facade. The actual signature (`src/SipiImage.hpp:384`) is:

```cpp
void read(const std::string &filepath,
          const std::shared_ptr<SipiRegion> &region = nullptr,
          const std::shared_ptr<SipiSize> &size = nullptr,
          bool force_bps_8 = false,
          ScalingQuality scaling_quality = {...});
// write(ftype, filepath, params) where ftype ∈ {"tif","j2k","png","jpg"}
```

There is **no in-memory-buffer decode API** — `read()` opens the file each call. Two consequences:

- A decode benchmark over `read()` includes `open()` + read syscalls. For large images the decode dominates, but the I/O is in the measured path.
- **Decision:** benchmark `read()` as-is, with fixtures staged on the Bazel `TEST_TMPDIR` (tmpfs where available). Do **not** add a buffer-source seam in this plan — that is a new public API and scope creep. If profiling later shows `open()` noise dominating, a `read_from_memory` seam becomes its own scoped task. (`SipiImage` also exposes `readSource()` — `SipiImage.hpp:409,419` — the server's actual read path, which additionally recomputes the Essentials-packet checksum per ADR-0009/0010. We benchmark `read()` to isolate the *codec* hot path without that checksum overhead; a separate `readSource` "server-read" benchmark can be added later if the checksum cost is of interest.)

**Benchmark surface.** One benchmark binary per concern under a dedicated package (see below), each using a `benchmark::Fixture` that reads + (for encode benchmarks) decodes the source **once in `SetUp()`**, keeping I/O and source-decode out of the timed loop:

```cpp
class DecodeFixture : public benchmark::Fixture {
  void SetUp(benchmark::State&) override { src_ = sipi::test::data_dir() + "/<fixture>"; }
  std::string src_;
};
BENCHMARK_DEFINE_F(DecodeFixture, Tile)(benchmark::State& state) {
  const int64_t dim = state.range(0);
  // SipiRegion(int x, int y, size_t w, size_t h) — include/iiifparser/SipiRegion.h:58
  auto region = std::make_shared<Sipi::SipiRegion>(/*x=*/0, /*y=*/0, dim, dim);
  // SipiSize has NO (int,int) ctor (SipiSize.h: only default / int reduce / float / string);
  // express a tile size via the IIIF size-string ctor (SipiSize.h:112):
  auto size   = std::make_shared<Sipi::SipiSize>(std::to_string(dim) + "," + std::to_string(dim));
  for (auto _ : state) {
    Sipi::SipiImage img;
    img.read(src_, region, size, /*force_bps_8=*/false, scaling_quality);
    benchmark::DoNotOptimize(img.getNx());  // getNx/getNy public, SipiImage.hpp:202,207;
    benchmark::ClobberMemory();             // read()'s file I/O side effects also prevent elision
  }
  state.SetBytesProcessed(int64_t(state.iterations()) * dim * dim /* * channels (accessor at impl) */);
}
BENCHMARK_REGISTER_F(DecodeFixture, Tile)->RangeMultiplier(2)->Range(256, 1024);
```

**Scenario matrix.** Organized around the PDF's full pipeline — **Decode → Process → Transcode** — plus the per-request parser front door. The PDF measured only the two ends; the Process stage is where its hardware-acceleration story lives.

- *Decode (input formats):* uncompressed tiled-pyramid TIFF (baseline), ZStd-TIFF, WebP-TIFF, Kakadu JP2, Kakadu HTJ2K, plain JPEG, and **untiled flat TIFF** + **full-resolution-tile-from-plain-JPEG** as the deliberate slow baselines (PDF slides 18, 23 — the ~300× penalty). Tile dimensions parameterized via `state.range()`.
- *Process (image-processing operators — the PDF's middle stage and its hardware-acceleration target):* `scaleFast`/`scaleMedium`/`scale` (bilinear resampling — exactly what the PDF accelerated via Intel IPP/SIMD), `rotate`, `crop`, `to8bps`, `convertToIcc` (ICC colour transform), `removeChannel`. All public `SipiImage` methods (`SipiImage.hpp:472-568`); decode a source once in `SetUp()`, then time each operator at representative parameters. **Several operators need a specifically-shaped source** (`convertToIcc` → embedded-ICC source + target profile; `to8bps` → 16-bps source; `removeChannel` → alpha/extra-channel source) — see the Process-tier fixture requirements under Dependencies. Completes pipeline coverage **and** establishes the baseline against which future SIMD/IPP/GPU acceleration is measured (the "exploiting hardware acceleration" half of the PDF — IIPImage 1.4 roadmap).
- *Encode (output transcode):* JPEG (Q=75/90), WebP (Q=90), PNG.
- *Parse (IIIF URL — per-request front door, lowest-effort benchmark):* the `SipiRegion`/`SipiSize`/`SipiRotation`/`SipiQualityFormat`/`SipiIdentifier` string parsers (`include/iiifparser/`). Pure CPU, **no fixtures**; the parser already has a fuzz target (`//fuzz/handlers:iiif_handler_uri_parser_fuzz`) confirming the entry point.
- *Tier 2 (cheap regression guards, add opportunistically):* `read_shape` across formats incl. the JP2 UUID-box fast path (info.json hot path, `SipiImage.hpp:433`); `SipiCache::check` canonical-URL lookup (`SipiCache.h:161` — note it touches the filesystem, so a small end-to-end micro, not pure CPU).
- **Out of scope:** AVIF / JPEG-XL (SIPI does not emit them), pass-through (not implemented), the cross-server comparison (would require installing iipsrv/cantaloupe/digilib/rais), and the e2e HTTP layer (deferred).

**Determinism for encode (and ICC) benchmarks.** Encode paths embed an ICC profile whose creation date is wall-clock-stamped (ADR-0002). The `just bench` recipe sets `SOURCE_DATE_EPOCH=946684800` and `SIPI_WORKSPACE_ROOT="."` (mirroring `test/approval/BUILD.bazel:44-47`) so allocation/codegen in the measured path is stable and any incidental byte output is reproducible. The Process tier's `convertToIcc` also touches the ICC machinery, so its target carries the same `env` (the File Manifest sets `env` for encode **and** process targets, not parse).

**Build shape, location & isolation.**
- **Location = co-located with the module, per ADR-0003** (`status: proposed`; the codebase is moving to `src/<module>/{Foo.cpp, Foo.h, Foo_test.cpp}` with flat-style includes, dissolving `test/unit/<component>/` and the `include/<mod>/` shadow into the modules). Benchmarks follow the same rule: `src/formats/<...>_benchmark.cpp` beside the handler source **today**, moving to `src/format_handlers/` **with the module** when that ADR-0003 PR lands. This is the Abseil / Bloomberg-BDE / Chromium convention ADR-0003 itself cites (`*_benchmark.cc` beside the code). **We deliberately front-run ADR-0003 for benchmarks** — this is *not* zero-cost: today the bench `cc_binary` deps `//src:sipi_lib` (the God-library ADR-0003 replaces) and the format headers still live in `include/formats/`, so when the module is promoted the bench's `deps` line and BUILD location move *with* the module — one small step, no worse than the module's own promotion.
- **Not a new `//test/bench/` tree.** No such directory exists today; creating one would run against ADR-0003's direction of dissolving `test/`-side trees into the modules. Benchmarks belong with the code they measure, not in a fresh top-level test tree (the concern that prompted this decision).
- Each benchmark is a **separate `cc_binary`** (never `cc_test`, never part of a `cc_library`) with `tags = ["manual"]`, declared in the module's `BUILD.bazel`. Depends on the module library (today `//src:sipi_lib`; post-ADR-0003 the per-module `cc_library`) + `//test:test_paths` + `@google_benchmark//:benchmark_main`. Explicit `load("@rules_cc//cc:defs.bzl", "cc_binary")` (Bazel 9 removed the global rule).
- **Coverage / production isolation via glob exclude.** `//src:sipi_lib` globs `src/**/*.cpp` with an `exclude` (`src/BUILD.bazel:250-263`, today only `SipiError.cpp`). Add `**/*_benchmark.cpp` to that exclude so bench sources never enter the production library or the `instrumentation_filter=//src` coverage build. The bench `cc_binary` is a dep of no test, so `bazel coverage //test/...` never builds or instruments it. (ADR-0003 will extend the same exclude to `**/*_test.cpp` when unit tests move into `src/`.)
- **Opt-only, never instrumented.** Built `-c opt` to match production; never under `--config=asan/ubsan/fuzz`. This also dodges the Linux/Clang/Debug Kakadu palette SIGILL (documented for the JP2 encode path). Stated explicitly in the recipe and acceptance criteria, mirroring how smoke docs note "asan does not apply."
- Excluded from `just bazel-test` / `just bazel-coverage` by the `manual` tag + the glob exclude. Verify `bazel test //...` does not select it.

**Before/after workflow (local, never a CI gate).**

```bash
just bench --benchmark_repetitions=20 --benchmark_out=before.json --benchmark_out_format=json
# ... make the hot-path change ...
just bench --benchmark_repetitions=20 --benchmark_out=after.json --benchmark_out_format=json
python3 <compare.py> benchmarks before.json after.json
```

`compare.py` ships in the upstream google/benchmark repo (`tools/compare.py`, needs scipy/numpy). **Sourcing mechanism — RESOLVED (2026-06-10, see Resume State decision 3):** the upstream `@google_benchmark//tools:compare` py_binary is *unconsumable* (its `rules_python` + `tools_pip_deps` pip hub are `dev_dependency = True`, dropped by Bzlmod for non-root modules). So we **add `rules_python` + our own dev pip hub** (`numpy==2.4.1`, `scipy==1.17.0`) and **vendor** `compare.py` + `gbench/` under `tools/benchmark/` pinned to 1.9.5, exposed as `//tools/benchmark:compare`. `just bench-compare` runs `bazel run //tools/benchmark:compare -- benchmarks before after`. It runs a Mann-Whitney U-test and prints per-benchmark deltas + an `OVERALL_GEOMEAN`.

**Regression decision rule (the discipline's teeth, lives in REVIEW.md).** On Apple Silicon you cannot disable turbo/throttle; rely on statistics:
- Demand baseline `_cv ≤ ~2%`; if a benchmark's CV is 5–10% the machine is too noisy (thermal/background) — re-run cooler or raise `--benchmark_min_time`.
- Trust a delta only if it is **green (p < 0.05) AND the median shift exceeds the baseline CV**. Treat sub-3% deltas as noise; trust ≥5% green shifts.
- **Same machine, same `-c opt` binary, for both before and after.** Never compare a Mac "before" against a Linux "after." Written into the convention.

#### Phase 2 — toolchain swap architecture

**The module.** BCR `llvm` v0.8.6 = `hermeticbuild/hermetic-llvm` (FOSDEM 2026). "Zero-sysroot, fully hermetic C/C++ cross-compilation toolchain." Compiles glibc/musl, libc++, compiler-rt from source per target (no external sysroot tarballs); downloads the macOS SDK hermetically from Apple's CDN (no host Xcode CLT). It is a **config rewrite**, not a version bump — the API is platform-constraint-based, not `llvm.toolchain(...)` attributes.

**MODULE.bazel (replace lines ~296-425).**
```starlark
bazel_dep(name = "llvm", version = "0.8.6")
llvm_source = use_extension("@llvm//extensions:llvm_source.bzl", "llvm_source")
llvm_source.version(llvm_version = "19.1.7")   # confirm prebuilt covers 19.x, else bootstrap cost
toolchain = use_extension("@llvm//extensions:toolchain.bzl", "toolchain")
toolchain.exec(arch = "aarch64", os = "macos")
toolchain.exec(arch = "x86_64", os = "linux")
toolchain.target(arch = "aarch64", os = "macos")
toolchain.target(arch = "x86_64", os = "linux")
toolchain.target(arch = "aarch64", os = "linux")
use_repo(toolchain, "llvm_toolchains")
register_toolchains("@llvm_toolchains//:all")
```
`cxx_standard` has no extension attr — already covered by the global `--cxxopt=-std=c++23` / `--host_cxxopt` in `.bazelrc:59-60`. libc++ is the default (the `stdlib={linux:libc++}` map disappears for the main toolchain). MVS will bump `rules_cc` (0.2.18→0.2.19) and `platforms` (1.0.0→1.1.0) — verify the prometheus-cpp `cc_library`-load patch survives.

**Deletions (the hermeticity win).**
- The `sysroot = use_repo_rule(...)` binding + both `sysroot(name="sysroot_linux_*")` blocks + both `llvm.sysroot(...)` calls (MODULE.bazel:314-355) — Linux sysroots are now bundled. Removes the `commondatastorage.googleapis.com/chrome-linux-sysroot` dependency and the Chromium `sysroots.json` bump procedure.
- `.bazelrc` macOS hacks: `-nostdinc++` + `-isystem .../MacOSX.sdk/...c++/v1` (166-169), `DEVELOPER_DIR`/`SDKROOT` repo_env (193-194). Keep `--repo_env=PATH` (Kakadu `gh` download + foreign_cc autotools still need it).
- `--action_env=BAZEL_DO_NOT_DETECT_CPP_TOOLCHAIN=1` (likely removable — test).
- **`bazel/glibc23_compat.c`** (the `2.42→2.31` alias shim): its own header says it becomes deletable once the build no longer sees the host runner's glibc. Deleting it and proving Kakadu still links against the bundled sysroot is the **strongest single hermeticity signal** in the acceptance gate.

**Retained couplings (honest scope).** The hermetic *compiler* does not give hermetic *autotools*. `--action_env=PATH`, `ACLOCAL_PATH`, `NIX_LDFLAGS` stay — libmagic's `autoreconf`, openssl's `perl Configure`, and Kakadu's Nix-wrapped sub-make still need Nix-provided `autoconf`/`automake`/`perl`. So "removes host Nix coupling" is **partial**: toolchain coupling goes, build-tool coupling remains.

**The fuzz second toolchain.** Today `llvm_toolchain_fuzz` is scoped via `extra_target_compatible_with(["//tools/fuzz:fuzz_enabled"])` — which has **no port** in the new module. The fuzz path already uses libc++ (not libstdc++), and modern libFuzzer (LLVM≥16) ships its own private libc++. **Plan:** drop the second toolchain; let the single registered `llvm_toolchains` serve the `fuzz_enabled` platforms. Validate libFuzzer links on linux-x86_64 and darwin-aarch64. Fall back (if needed) to a `cxxstdlib` constraint on the fuzz platform.

**Coverage path (high-risk, must gate).** Coverage resolves `llvm-profdata`/`llvm-cov` from the Nix dev-shell PATH (`justfile` + `COVERAGE_GCOV_PATH`/`LLVM_COV`) with `--features=llvm_coverage_map_format`. With a hermetic toolchain these must resolve to the **toolchain's** binaries (version-matched to the instrumented `.profraw`), or lcov comes back empty. Explicit acceptance criterion.

**Cross-compilation is a motivation, NOT a Phase-2 gate.** darwin→linux-aarch64 (so a Mac builds the Linux Docker image locally) is *supported* by the module out of the box, but proving it end-to-end (Kakadu `libkdu` under cross + `glibc23_compat` deletion holding under cross + `rules_oci` producing a loadable arm64 image from darwin) is a separate, multi-day effort. It is **explicitly out of Phase 2's definition of done** to prevent scope creep; tracked as a follow-up once the same-host swap is green.

**Rollback.** No clean runtime-flag coexistence (the two modules wire sysroots structurally differently). Rollback unit = **the PR, reverted wholesale**. Land Phase 2 on its own branch; keep `toolchains_llvm` as default on `main` until the new path is green on all three platforms.

### Implementation Phases

#### Phase 1: Microbenchmark package (foundation + discipline)

Tasks:
- [x] `MODULE.bazel`: `bazel_dep(name = "google_benchmark", version = "1.9.5", dev_dependency = True)`.
- [x] Add benchmark `cc_binary` target(s) **in `src/BUILD.bazel`** (NOT `src/formats/BUILD.bazel` — see Resume State decision 1; new sub-package BUILD breaks the `:sipi_lib` glob) (`tags=["manual"]`, `testonly=True`, deps module lib + `//test:test_paths` + `@google_benchmark//:benchmark_main`, `env={SIPI_WORKSPACE_ROOT:".", SOURCE_DATE_EPOCH:"946684800"}`, `data=[<fixtures>]`); add `**/*_benchmark.cpp` to the `//src:sipi_lib` glob exclude (`src/BUILD.bazel:256-261`). **DONE:** glob exclude + parse/process/decode/encode targets (decode/encode resolve `@sipi_bench_fixtures` via `SIPI_BENCH_FIXTURES_DIR` exported by the recipe from the runfiles tree — external-repo files are invisible to workspace-relative test_paths under direct exec).
- [x] Benchmark sources covering the full matrix, co-located with the module measured (sources under `src/formats/`, `src/`, `src/iiifparser/`; BUILD in `src/BUILD.bazel` until ADR-0003 promotes the module): Decode + Encode in `src/formats/`, Process in `src/process_benchmark.cpp` (with `SipiImage`), Parse in `src/iiifparser/parse_benchmark.cpp`. **DONE 2026-06-10:** all four tiers running at -c opt (see Session findings above). Encode matrix adapted: jpg75/jpg90/png/tif/jpx (no WebP emission in SIPI; HTJ2K license-gated). Tier-2 (`read_shape`, cache `check`) remains opportunistic follow-up.
- [x] **Fixtures** — **DONE 2026-06-10** (superseding decision: dsp-ci-assets, v1 = big_building matrix only): `tools/benchmark/generate_fixtures.sh` (pinned source zip sha256 + vips 8.18.2 + Kakadu v8.7 kdu_compress + SOURCE_DATE_EPOCH; ships the imagecompression.info notice) → `sipi-bench-fixtures-v1.tar.zst` (321 MB) on dsp-ci-assets, fetched via generalized `gh_release_archive` into `@sipi_bench_fixtures` (dev_dependency, lazy). All variants verified decodable by sipi 5.0.1 before upload. big_tree + Bellini → later release.
- [x] `just bench` + `just bench-compare` recipes (build `-c opt`, run binary directly à la `bazel-run-fuzz`; **compare = rules_python `bazel run //tools/benchmark:compare`**, see Resume State decision 3 — NOT `@google_benchmark//tools:compare.py`). **DONE 2026-06-10** (+ discovered fix: `.bazelrc` `-U_FORTIFY_SOURCE` before the `=2` redefine — Bazel's opt feature injects `=1`, breaking `-Werror` deps like google_benchmark at `-c opt`). Fixture host decided: **dsp-ci-assets release via the kakadu_archive pattern**.
- [x] Docs: **new `docs/src/development/benchmarking.md`** how-to (mirrors `fuzzing.md`), wired into `docs/mkdocs.yml` nav + search map; supersede the aspirational note in `testing-strategy.md` with a pointer; `CLAUDE.md` + `REVIEW.md` convention text (below) linking to it; link from `developing.md`. **DONE 2026-06-10.**

Success criteria: `just bench` runs green on darwin-aarch64 and linux-x86_64 (**linux-aarch64 not required** — the bench is a local dev-loop tool, not CI-gated, and aarch64-Linux is the least-available platform), produces JSON, and `compare.py` yields a stable geomean across two no-change runs (CV ≤ 2% on the baseline). Quantifies the PDF's concern intra-SIPI: full-resolution tile decode from plain JPEG is **≥100× slower** than from a tiled-pyramid TIFF of the same image (slide-23 direction; the exact multiple is recorded, not asserted), and per-format decode/encode cost is a stable table. **This does NOT reproduce the PDF's cross-server, end-to-end claim** — that is deferred; Phase 1 measures intra-SIPI per-stage cost. Effort: **M–L** (≈5–8 days; fixture sourcing + the Process-tier shaped fixtures are the variable).

#### Phase 2: Hermetic LLVM toolchain swap

Tasks:
- [ ] Branch. Rewrite the MODULE.bazel toolchain block; delete Chromium-sysroot rules + macOS `.bazelrc` hacks; pin `llvm` + LLVM version.
- [ ] Fold the fuzz toolchain into the single registered toolchain; validate libFuzzer on linux-x86_64 + darwin-aarch64. Note: collapsing to one toolchain makes the `fuzz_enabled` first-registered-wins ordering (MODULE.bazel:416-423) a no-op for toolchain selection — confirm the `--config=fuzz` per-file-copt and `tools/fuzz/BUILD.bazel` do not otherwise depend on that ordering.
- [ ] Re-validate `--dynamic_mode=off` **specifically on linux-aarch64** (exiv2 `.a`-only + libc++.a PIC reasons).
- [ ] Delete `bazel/glibc23_compat.c`; prove Kakadu links against the bundled sysroot.
- [ ] Re-prove macOS C++23 `<expected>` ABI against the CDN-fetched SDK libc++ (the most likely silent break).
- [ ] Re-point coverage `llvm-cov`/`llvm-profdata` to the toolchain binaries.
- [ ] Per-foreign_cc-dep verification matrix (the long pole) — for each of the **9 `ext/` foreign_cc deps + Kakadu** (the BCR `bazel_dep` C/C++ deps use standard `rules_cc` and are lower-risk — spot-check zlib/openssl only): (1) `.a` produced; (2) **symbol-version sanity** via `nm`/`readelf` that it references the *bundled-sysroot* glibc symbol versions, not the host's 2.42 (the `__isoc23_*`/`fcntl64` class the glibc shim papers over); (3) the consuming SIPI target links and the relevant approval/unit test passes. Re-verify the three documented escape-hatches (per-file-copt sanitizer, CMAKE_BUILD_TYPE pin, `-fsanitize=fuzzer` LDFLAGS probe). The Kakadu `.a` + glibc-shim-deletion proof (above) uses exactly this symbol check as its acceptance procedure.
- [ ] Measure remote-cache hit-rate / first-build-time delta (Apple SDK CDN + source-built Linux sysroots vs old Chromium-tarball flow through the Cloud Run proxy).
- [ ] **Run the Phase-1 microbench before/after the swap** on a fixed host; record codegen perf delta in the PR.
- [ ] Write **ADR-0014** (toolchain provider swap): why hermetic-llvm, the constraint-model rewrite, glibc-shim deletion, macOS-SDK-from-CDN tradeoff, cache implications, and a concrete **pinning policy** for the v0.8.x / compatibility-level-0 / ~weekly-release churn: (1) pin an *exact* version (`0.8.6`), never a range — compat-level 0 gives no cross-version guarantee; (2) bump only on a real trigger (an LLVM version we need, or a fix we require), never speculatively; (3) every bump re-runs the full Phase-2 equivalence gate (approval goldens + 3-platform build + coverage + sanitizer/fuzz), since codegen can shift; (4) keep `toolchains_llvm` removable for one release cycle as the revert unit.

Success criteria — the equivalence gate (all on linux-x86_64, linux-aarch64, darwin-aarch64): existing approval goldens pass **unchanged** (the swap introduces no codec/ICC byte drift); unit + e2e + smoke green; coverage lcov non-empty on linux-amd64; sanitizer + fuzz build and run; `glibc23_compat.c` gone + Kakadu links; Docker image builds + smoke passes both arches; microbench shows no unexplained perf regression. Effort: **L** (≈2–3 weeks for a single IC; foreign_cc cross-validation and the macOS `<expected>` ABI re-proof are the long poles).

#### Phase 3 (follow-up, out of this DoD): darwin→linux cross-compile

Prove `just bazel-docker-build-arm64` works from a Mac. Gated separately once Phase 2 lands. Effort: **M–L**, real risk on Kakadu + foreign_cc under cross.

## Alternative Approaches Considered

- **Criterion (Rust) for microbenchmarks.** Rejected: criterion is Rust-only; the codec is C++ today. A niche C++ port (`p-ranav/criterion`) is unmaintained. Google Benchmark is the C++ standard, BCR-available, mirrors the existing `googletest` dev-dep.
- **End-to-end HTTP load layer first (reuse `test/e2e-rust`).** Deferred: cross-machine/cross-OS comparisons are apples-to-oranges, and CI load tests on shared runners are too noisy to gate. The discipline lives in the local microbench loop.
- **Keep `toolchains_llvm`; add only a Linux sysroot for cross-compile.** Rejected: leaves the macOS Nix/Xcode-CLT coupling and the Chromium-sysroot maintenance in place. The `llvm` module removes both, which is the stated goal.
- **Git LFS for fixtures.** Rejected in favor of `http_archive` (pin-by-sha256) — matches the repo's existing hermeticity pattern (FFmpeg static, sysroots, ApprovalTests), avoids adding LFS (clone-time, quota, CI fetch) to a repo that has none.
- **Synthetic fixtures.** Considered as fallback. Rejected as primary: synthetic gradients/noise compress and decode unrealistically (noise won't compress; gradients compress too well), distorting codec-cost numbers. A real CC0 photographic artwork is representative.
- **Toolchain-first ordering (as originally requested).** Flipped after spec review — see Overview.

## File Manifest

### Phase 1 (created / modified)
- `MODULE.bazel` — add `bazel_dep(name = "google_benchmark", version = "1.9.5", dev_dependency = True)`; add the fixture `http_archive` (sha256-pinned).
- `src/formats/decode_benchmark.cpp`, `src/formats/encode_benchmark.cpp` — **new** (Decode + Transcode tiers), co-located with the source per ADR-0003 (move to `src/format_handlers/` with the module when that ADR-0003 PR lands). Flat-style includes.
- `src/process_benchmark.cpp` — **new** (Process tier: scale/rotate/crop/to8bps/convertToIcc/removeChannel), co-located with `SipiImage` (its own module post-ADR-0003).
- `src/iiifparser/parse_benchmark.cpp` — **new** (Parse tier), co-located with the iiifparser module. No fixtures.
- `src/BUILD.bazel` — add benchmark `cc_binary` target(s) **here, in the existing `//src` package** (NOT new `src/formats/BUILD.bazel` / `src/iiifparser/BUILD.bazel` — Resume State decision 1; that breaks the `:sipi_lib` glob): `tags=["manual"]`, `testonly=True`, `env={SIPI_WORKSPACE_ROOT, SOURCE_DATE_EPOCH}` (encode/process only), `data=[<fixtures filegroup>]` (parse needs none), deps the module lib + `//test:test_paths` + `@google_benchmark//:benchmark_main`. **DONE:** `//src:parse_benchmark`.
- `src/BUILD.bazel` — add `**/*_benchmark.cpp` to the `:sipi_lib` glob `exclude` (was only `SipiError.cpp`, lines 256-261). **DONE.**
- `justfile` — add `bench` and `bench-compare` recipes.
- `MODULE.bazel` / `tools/benchmark/` — **NEW (Resume State decision 3):** `bazel_dep(rules_python, 1.7.0)` + `sipi_bench_pip` pip hub (`numpy==2.4.1`, `scipy==1.17.0`) + 3.12 python toolchain; vendor `tools/benchmark/{compare.py, gbench/*}` (pinned 1.9.5); `tools/benchmark/{BUILD.bazel, requirements.txt}`; `py_binary //tools/benchmark:compare`.
- ~~`flake.nix` — add `python3` + scipy/numpy~~ — **DROPPED** (the `sipi_bench_pip` hub supplies them hermetically).
- `CLAUDE.md` — "hot-path changes require a benchmark" subsection.
- `REVIEW.md` — Performance / Always-check rule.
- `docs/src/development/benchmarking.md` — **new** dedicated how-to page (mirrors `fuzzing.md`).
- `docs/mkdocs.yml` — add `benchmarking.md` to `nav:` (after "Fuzz Testing") **and** the search-description map.
- `docs/src/development/testing-strategy.md` — replace the aspirational microbench note with a strategy-level pointer to `benchmarking.md`.
- `docs/src/development/developing.md` — link to `benchmarking.md`.

### Phase 2 (modified / deleted / created)
- `MODULE.bazel` — replace the toolchain block (~296-425): swap `toolchains_llvm`→`llvm`; delete the `sysroot` repo-rule binding + both `sysroot(...)`/`llvm.sysroot(...)` blocks; drop the second (fuzz) toolchain.
- `.bazelrc` — delete macOS SDK hacks (166-169, 193-194); test-remove `BAZEL_DO_NOT_DETECT_CPP_TOOLCHAIN` (16); re-validate `--dynamic_mode=off` (79), the sanitizer per-file-copt escapes (216-420), and coverage env.
- `bazel/glibc23_compat.c` — **delete** (+ its `//bazel:glibc23_compat` BUILD target and the `//src:sipi_lib` deps `select`).
- `tools/fuzz/BUILD.bazel` — adjust if folding the fuzz toolchain into a constraint.
- `justfile` — re-point coverage `llvm-cov`/`llvm-profdata` to the toolchain binaries.
- `docs/adr/0014-toolchain-provider-swap.md` — **new** ADR (with the v0.8.x pinning policy).
- `docs/src/development/bazel.md`, `building.md` — update toolchain section.

> Manifest is indicative, not exhaustive — Phase 2's foreign_cc per-dep matrix may surface additional `.bazelrc`/`ext/` touch-ups.

## Acceptance Criteria

### Functional Requirements
- [~] `just bench` produces Google Benchmark JSON for the full matrix (Decode / Process / Encode / Parse), `-c opt`, on darwin-aarch64 and linux-x86_64 (linux-aarch64 out of scope for the bench loop). **darwin-aarch64 verified 2026-06-10 (all four tiers); linux-x86_64 run still outstanding** (no local Linux box this session — verify via OrbStack/colima or a Linux checkout).
- [x] `just bench-compare before.json after.json` prints per-benchmark U-test deltas + geomean. **Verified** (two no-change parse runs: sub-3% noise-level deltas).
- [x] Benchmarks quantify the slow-baseline penalty intra-SIPI as a recorded number: **full-res 256px tile from plain JPEG = 219 ms vs 0.96 ms from tiled-pyramid TIFF ≈ 228×**. (Untiled-flat TIFF tile reads turn out NOT slow — libtiff strip access reads only the needed rows of an uncompressed flat TIFF; the real flat-TIFF penalty shows in the thumbnail shape, 490 ms.) Not the PDF's cross-server claim, which stays deferred.
- [ ] (Phase 2) Hermetic toolchain builds `//src/cli:sipi` and the full test suite on all three platforms.

### Non-Functional Requirements
- [x] Microbench excluded from `bazel test //...` and `bazel coverage` (manual `cc_binary` + `**/*_benchmark.cpp` glob exclude on `//src:sipi_lib`); `just bazel-test` (wildcards incl. `//src/...`) ran 2026-06-10 without selecting or building any benchmark target.
- [ ] Baseline CV ≤ 2% on a quiesced dev machine — formal 20-repetition baseline not yet run (machine was busy with builds all session); run before the first real before/after use.
- [ ] (Phase 2) No unexplained perf regression vs the pre-swap microbench baseline.

### Quality Gates
- [ ] (Phase 2 equivalence gate) Existing approval goldens pass unchanged on all 3 platforms (no codec/ICC byte drift); unit/e2e/smoke green; coverage lcov non-empty; sanitizer + fuzz build/run; `glibc23_compat.c` deleted + Kakadu links; Docker image + smoke on both arches.
- [ ] ADR-0014 merged.
- [ ] `docs/src/development/benchmarking.md` published (wired into `mkdocs.yml` `nav:` + search-description map); `CLAUDE.md` + `REVIEW.md` convention text merged, linking to it.

## Success Metrics

- A reproducible, version-controlled number for SIPI's per-format tile decode/encode cost — the thing we currently cannot produce.
- Every future hot-path PR carries a before/after `compare.py` table in its description.
- Phase 2: the count of `.bazelrc`/`MODULE.bazel` workarounds drops (delete Chromium sysroots, macOS SDK hacks, glibc shim); a Mac can (Phase 3) build the Linux image locally.

## Dependencies & Prerequisites

- **`google_benchmark` 1.9.5** (BCR, dev_dependency). Targets `@google_benchmark//:benchmark` / `:benchmark_main`. Transitive `libpfm` benign on macOS.
- **`llvm` 0.8.6** (BCR = hermeticbuild/hermetic-llvm). Bumps `rules_cc`→0.2.19, `platforms`→1.1.0 via MVS.
- **Fixtures (blocking for Phase 1 completeness)** — two distinct needs:
  - *Decode / Process-perf source (large, photographic):* **SOURCE DECIDED 2026-06-10 → imagecompression.info `big_building`/`big_tree` (16-bit lossless PPM) as primary + Bellini Commons (30,000×26,319, PD) as the giant master. See Resume State decision 4. The Met/Cleveland museum-API text below is SUPERSEDED** (live API not byte-stable; lossy JPEG source leaks artifacts into "lossless" variants). The generation recipe below stays valid for whichever master. ~~use the **Met `/original/` JPEG** (`collectionapi.metmuseum.org`, CC0, no key, full native resolution — often 5–12k px) or **Cleveland `images.full` TIFF** (`openaccess-api.clevelandart.org`, CC0, uncompressed) as the scriptable high-res source. **Not AIC** — its IIIF endpoint is capped at ~1686px, too small.~~ Pin the source URL + **source SHA-256**, and verify with `vipsheader` the dimensions. Exact 15016×11741 dims are a *target spec, not a requirement* — we measure intra-SIPI cost, not Pillay's absolute numbers. Generate the variants with a **checked-in generator following the existing precedent** (`test/unit/sipiimage/fixtures/{generate_jpeg_fixtures.py, generate_bilevel_tiffs.cpp}` — sources committed, large outputs git-ignored); host the set as one sha256-pinned `http_archive` tarball (GCS / GitHub release), multi-GB, out of the working-tree clone. Verified recipe (libvips 8.17; `tiffcp` is **not** a pyramid generator — use vips):
    ```bash
    vips copy source.jpg source.tif
    vips tiffsave source.tif pyr-none.tif --tile --pyramid --compression none --tile-width 256 --tile-height 256 --bigtiff
    vips tiffsave source.tif pyr-zstd.tif --tile --pyramid --compression zstd --level 9 --tile-width 256 --tile-height 256
    vips tiffsave source.tif pyr-webp.tif --tile --pyramid --compression webp --Q 90  --tile-width 256 --tile-height 256
    vips jpegsave source.tif baseline.jpg --Q 90          # slow baseline: plain JPEG
    vips tiffsave source.tif flat.tif --compression none  # slow baseline: untiled flat TIFF
    # JP2 / HTJ2K via Kakadu v8.7 (the repo's current pin), slide-14 params:
    kdu_compress -i source.tif -o pyr.jp2 -rate 2.5 Clayers=1 Clevels=7 Cprecincts="{256,256}" \
      Corder=RPCL Cblk="{64,64}" ORGgen_plt=yes ORGplt_parts=R ORGtparts=R ORGgen_tlm=8 Cuse_sop=yes
    kdu_compress -i source.tif -o pyr.jph Cmodes=HT -rate 2.5 Clayers=1 Clevels=7 Cprecincts="{256,256}" \
      Corder=RPCL Cblk="{64,64}" ORGgen_plt=yes ORGplt_parts=R ORGtparts=R ORGgen_tlm=8 Cuse_sop=yes   # HTJ2K → .jph
    ```
    **Byte-reproducibility** needs the whole chain pinned: source SHA-256 + `libvips`/`libtiff`/`libzstd`/`libwebp` versions + Kakadu v8.7 + every flag explicit (no tool defaults), and `SOURCE_DATE_EPOCH=946684800` to normalize embedded TIFF/JP2 timestamps (same discipline as ADR-0002). Run generation in the Nix dev shell so the libs are pinned.
  - *Process-tier shaped sources (small, correctness — distinct from the large baseline):* the Process operators need specifically-shaped inputs the photographic baseline does not provide — `convertToIcc` needs an embedded-ICC source **plus** a named target profile; `to8bps` needs a 16-bps source; `removeChannel` needs an alpha/extra-channel source; `scale`/`rotate` a known dimension. **Audit `test/_test_data/images/`** (subdirs `jpeg/`, `unit/`, `iso-15444-4/`, `knora/`) for existing small fixtures meeting these shapes and reuse them; generate any missing ones via the same `generate_*` precedent. These stay small and checked-in (not in the http_archive tarball).
- **`compare.py`**: ~~scipy/numpy in the dev shell (`flake.nix`)~~ — SUPERSEDED (2026-06-10). Now via `rules_python` + the `sipi_bench_pip` hub (`numpy==2.4.1`, `scipy==1.17.0`); `flake.nix` python is no longer needed. See Resume State decision 3.
- Kakadu access (`gh auth`, dasch-swiss org) — unchanged.
- **ADR-0003 is `proposed`, not accepted.** The benchmark co-location rests on its direction holding. If ADR-0003 is rejected or materially changes, benchmarks stay flat in `src/` beside the handler/`SipiImage` sources and the location section is revisited — cheap to adjust (a BUILD-file move + `deps` change).

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| `rules_foreign_cc` breaks against the fully-hermetic clang | H | H | Risk set is the **9 `ext/` foreign_cc deps** (exiv2, jansson, jbigkit, jpeg, lcms2, png, sentry, tiff, webp) **+ Kakadu** (hand-makefile) — NOT the BCR `bazel_dep` C/C++ deps (zlib/zstd/xz/sqlite3/…/openssl), which use standard `rules_cc` + handle platforms themselves (spot-check only). Per-dep matrix (below); re-verify the 3 escape-hatches; land on a branch; the long pole |
| macOS C++23 `<expected>` ABI breaks against CDN SDK libc++ | M | H | Explicit link test on darwin-aarch64 before anything else in Phase 2 |
| Coverage lcov comes back empty (llvm-cov/profdata resolution) | M | H | Gate criterion; re-point to toolchain binaries; verify on linux-amd64 |
| `--dynamic_mode=off` fails on linux-aarch64 (PIC libc++.a) | M | M | Re-validate specifically on aarch64 in CI (least-locally-tested platform) |
| `llvm` v0.8.x / compat-level-0 / weekly churn on sanitizer/link surface | M | M | Pin exact version; ADR-0014 pinning policy; keep `toolchains_llvm` revert path |
| Apple SDK CDN not cacheable via Cloud Run bazel-remote → CI cold-start regress | M | M | Measure first-build time; cache through the proxy if possible; document |
| Fixture licensing (cannot redistribute arbitrary artwork) | M | H | CC0 museum source only; document provenance; synthetic fallback |
| Microbench includes `open()` I/O in the measured path | L | M | Fixtures on tmpfs; profile; add memory-source seam later only if needed |
| Noisy Apple Silicon measurements mislead | M | M | CV gate + U-test + same-machine rule in REVIEW.md |
| darwin→linux cross-compile scope-creeps into Phase 2 | M | M | Explicitly deferred to Phase 3; not in Phase 2 DoD |
| Collapsing the fuzz toolchain makes `fuzz_enabled` a no-op for toolchain selection | M | M | Validate libFuzzer links on linux-x86_64 + darwin-aarch64; confirm nothing else keys off the constraint (`--config=fuzz` per-file-copt, `tools/fuzz/`); the old first-registered-wins ordering disappears by design, not by accident |

## Resource Requirements

Single IC (Ivan / engineering). Phase 1 ≈ M–L (≈5–8 days, fixture sourcing the variable), Phase 2 ≈ L (≈2–3 weeks). No new infra beyond a GCS/GH-release object for the fixture tarball. CI: 3-platform matrix already exists.

## Future Considerations

- Phase 3 darwin→linux cross-compile and local Docker image builds.
- A later e2e HTTP layer (staging-only, never CI-gated) if a same-environment cross-server comparison is ever wanted.
- Hardware acceleration (the PDF's IPP/CUDA angle, IIPImage 1.4 roadmap) is downstream of having a microbench to measure it.
- If/when the codec migrates to Rust, the microbench discipline carries over to `criterion` unchanged.

## Documentation Plan

- **CLAUDE.md** — new short subsection (under Testing or a new "Performance" heading):
  > **Hot-path changes require a benchmark.** Before changing any image decode/encode hot path (`src/formats/*`, `SipiImage` read/write, `iiifparser`), a Google Benchmark microbench must exist, co-located with the module (per ADR-0003) as a `*_benchmark.cpp` `cc_binary`. Add one if it doesn't. Justify the change with a before/after `just bench` + `compare.py` run on the same `-c opt` binary and machine. See `docs/src/development/testing-strategy.md`.
- **REVIEW.md** — under **Always check**:
  > ### Performance
  > - Hot-path changes (codec decode/encode, `SipiImage::read`/`write`, IIIF parsing) include a before/after microbenchmark comparison in the PR. No benchmark, no hot-path change.
  > - Trust a delta only if green (U-test p<0.05) AND median shift > baseline CV; same machine, same `-c opt` binary for before and after. Sub-3% is noise.
- **docs/src/development/benchmarking.md** — **new dedicated page** (mirrors `fuzzing.md`), the single source of truth for the suite: `just bench`/`bench-compare`, reading `compare.py` output, the regression decision rule + macOS noise/CV caveats, the ADR-0003 co-location convention, and the "no benchmark, no hot-path change" policy. Wire into `docs/mkdocs.yml` **`nav:`** (after "Fuzz Testing", ~line 24) **and** the plugin search-description map (~line 91) — both, as `fuzzing.md` is.
- **docs/src/development/testing-strategy.md** — replace the aspirational microbench note with a short strategy-level pointer to `benchmarking.md` (keep the tier's place in the pyramid; the how-to lives on the dedicated page).
- **docs/src/development/developing.md**, **CLAUDE.md**, **REVIEW.md** — link to `benchmarking.md` as the canonical how-to; do not duplicate the run instructions.
- **ADR-0014** — toolchain provider swap.
- **ADR-0015** (or fold into ADR-0003's consequences) — benchmark methodology: the "no benchmark, no hot-path change" policy, the co-location rationale, and the regression decision rule (U-test p<0.05 ∧ median > CV, same-machine). Captures the *why* as an architectural anchor, not only docs that can drift.
- **docs/src/development/bazel.md / building.md** — toolchain section updated post-swap.

## References & Research

### Internal References
- Codec API: `src/SipiImage.hpp:384` (`read`), `:457` (`write`), `:433` (`read_shape`); `include/SipiIO.h:126,167,179`.
- Unit-test BUILD pattern + decode/encode call: `test/unit/sipiimage/BUILD.bazel`, `test/unit/sipiimage/jpeg_write_test.cpp:56-58`.
- **Benchmark co-location direction:** `docs/adr/0003-module-co-located-source-and-tests.md` (`status: proposed`; `src/<module>/{Foo.cpp,Foo.h,Foo_test.cpp}`, `include/` deleted, per-module `cc_library` + `layering_check`; cites Abseil/BDE/Chromium). Future format-handler module: `src/format_handlers/` per `docs/archive/2026-05-08-modularization-analysis.md:301`.
- `//src:sipi_lib` glob + `exclude` (add `**/*_benchmark.cpp`): `src/BUILD.bazel:250-263`. `//test:test_paths` `test/BUILD.bazel`, `test/test_paths.hpp`.
- Codec API (verified): `SipiRegion(int,int,size_t,size_t)` `include/iiifparser/SipiRegion.h:58`; `SipiSize` has only default/`int`/`float`/`string` ctors (no `(int,int)`) `include/iiifparser/SipiSize.h:82-112`; `getNx`/`getNy` `src/SipiImage.hpp:202,207`.
- Approval target not platform-gated (single shared golden set): `test/approval/BUILD.bazel`.
- Toolchain block + foreign_cc + fuzz toolchain: `MODULE.bazel:296-425, 357-423, 493-589`.
- `.bazelrc` mechanisms: macOS SDK hacks 129-194, NIX_LDFLAGS 36-46, strict_action_env 10, static link 79, sanitizer/foreign_cc escape-hatches 216-420, coverage env.
- glibc shim (delete in Phase 2): `bazel/glibc23_compat.c` (2.42→2.31).
- Fuzz constraint/platforms: `tools/fuzz/BUILD.bazel`. Plain platforms: `bazel/platforms/BUILD.bazel`.
- Recipe template (build+exec a binary directly): `justfile:196` (`bazel-run-fuzz`); docker recipes 247-315.
- Dedicated-docs-page precedent: `docs/src/development/fuzzing.md`; mkdocs wiring in `docs/mkdocs.yml` (`nav:` line 24 + search-description map line 91). New page mirrors this in both places.
- Determinism: `docs/adr/0002-icc-profile-determinism-test-only.md`; `Icc::iccBytes()`.
- Aspirational benchmark note to supersede: `docs/src/development/testing-strategy.md:718-735, 913`.
- Memory budget metrics (load-time context): `docs/src/operation/memory-budget.md`.

### External References
- BCR `llvm`: https://registry.bazel.build/modules/llvm — hermeticbuild/hermetic-llvm: https://github.com/hermeticbuild/hermetic-llvm
- FOSDEM 2026 talk: https://fosdem.org/2026/schedule/event/F8SDAA-zero-sysroot_hermetic_llvm_cross-compilation_using_bazel/
- BCR `google_benchmark` 1.9.5: https://registry.bazel.build/modules/google_benchmark — https://github.com/google/benchmark
- Google Benchmark user guide / tools.md / reducing_variance.md (compare.py, fixtures, DoNotOptimize/ClobberMemory, flags).
- `rules_foreign_cc` hermetic-toolchain issues: #592, #1296.
- Fixture sources (CC0): Met Collection API https://metmuseum.github.io/ ; Cleveland Open Access API https://openaccess-api.clevelandart.org/ . Generation: libvips 8.17 `tiffsave` https://www.libvips.org/API/8.17/method.Image.tiffsave.html ; IIPImage "TIFF Image Encoding" (Pillay, Dec 2024) https://iipimage.sourceforge.io/2024/12/tiff-image-encoding-optimizing-for-size-speed-and-quality ; Kakadu usage examples https://kakadusoftware.com/wp-content/uploads/Usage_Examples.txt ; IIIF htj2k recipe https://github.com/IIIF/htj2k/issues/1 ; Code4Lib HTJ2K eval https://journal.code4lib.org/articles/17596 .
- Source PDF: `sipi/docs/benchmarks/01_Ruven_Pillay_Evaluating_IIIF_Server_Performance.pdf`.

### Institutional Learnings
- SIPI has no `docs/learnings/` directory. Closest prior art: the `specs/2026-04-16-sipi-nix-unified-build/` plans (build-system lineage, incl. `05-feat-icc-deterministic-timestamps`) and ADRs 0002, 0013.
