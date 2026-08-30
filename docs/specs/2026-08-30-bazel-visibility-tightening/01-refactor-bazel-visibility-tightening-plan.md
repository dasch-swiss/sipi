---
title: "refactor: enforce documented build-graph boundaries via Bazel visibility"
type: refactor
date: 2026-08-30
author: "Ivan Subotic"
status: reviewed
linear: DEV-7083
repository: dasch-swiss/sipi
repositories: []
---

# Enforce documented build-graph boundaries via Bazel visibility

## Overview

Tighten Bazel visibility repo-wide so that every documented "consumed only by X"
rule becomes an analysis-time build invariant instead of a comment. Two
workstreams:

- **A — visibility audit:** narrow every `//visibility:public` package/target
  (main repo and vendored-dep overlays) to its actual consumer set, established
  by a full grep-based consumer map.
- **B — ICC chokepoint enforcement:** convert the CLAUDE.md prose invariant
  "every codec emission routes ICC serialization through `Icc::iccBytes()`;
  never call `cmsSaveProfileToMem` directly" into structure, by removing lcms2
  types from `icc.h`'s public surface and narrowing `@lcms2` visibility.

All changes are BUILD-file edits plus one C++ header split. No runtime
behavior, no approval-golden, and no hot-path change (no benchmark required).

## Problem Statement / Motivation

One reason SIPI uses Bazel is to enforce the build graph — to make it hard to
depend on things not meant to be depended on. Today that enforcement is
partial:

- Most engine packages under `src/**` already carry
  `default_visibility = ["//src:__subpackages__"]`, but ten internal packages
  (`//src`, `//src/cli`, `//src/cli/rust`, `//src/server/rust`,
  `//src/image_processing`, the four `//src/format_handlers/corpus/*` packages,
  and `//src/iiifparser/corpus`) and all nine vendored-dep overlay repos
  (`bazel/*.BUILD.bazel`) are fully public (eight via package default;
  `@tiff` exports `:tiff` per-target).
- Several BUILD files carry comments that *are* visibility rules waiting to be
  written — most sharply `bazel/mimalloc.BUILD.bazel:17` ("Consumed only by
  `//src/cli/rust:sipi`; nothing else may depend on it") in the same file as
  `package(default_visibility = ["//visibility:public"])`. The allocator
  isolation of the C++ engine binary is a hard rule enforced by nothing.
- The ICC determinism invariant (CLAUDE.md, ADR-0002) is enforced only by
  convention plus an indirect approval-test failure: `icc.h:23` includes
  `<lcms2.h>` and `icc.h:149` (`getIccProfile()`) hands the raw `cmsHPROFILE`
  to every consumer, so any format handler can silently bypass
  `Icc::iccBytes()`.

## Proposed Solution

Apply the repo's own established tight-visibility patterns everywhere the
consumer map says they fit:

- `src/metadata/cpp/internal/BUILD.bazel:23` — internal package restricted to
  its parent (the canonical Test-seam pattern).
- `src/cli/cpp/commands/BUILD.bazel:29` — single-parent-package library.
- `src/iiifparser/cpp/classifier/BUILD.bazel:29` — per-target grant to one
  test package.
- `test/e2e/BUILD.bazel:68` — explicit `default_visibility = private` with
  named exceptions.

For overlay build files, visibility labels use the canonical main-repo form
`@@//pkg:__pkg__` (canonical-label precedent: `bazel/kakadu.BUILD.bazel:35-37`
— those are `select()` keys; this plan introduces the first `@@//`-form
visibility declarations).

For the ICC invariant: make `icc.h` lcms2-free (opaque profile handle), move
the lcms2-typed access into a header under the `src/metadata/cpp/internal`
seam, then narrow `@lcms2` to the packages that do legitimate color
management. A format handler then cannot obtain a typed profile handle
through `icc.h` and cannot add the `@lcms2` dep back (visibility error).
`layering_check` is deferred repo-wide (DEV-6353), so a TU that transitively
reaches `@lcms2` headers could still textually `#include <lcms2.h>`; that
residue is within the accident bar (see Non-goals) — the accident this closes
is the copy-a-codec path, where the profile comes from the `Icc` object.

Finally, write the repo-wide visibility policy into `CONVENTIONS.md` so the
default for new packages is documented once (per the no-bespoke-CI-gate rule:
the enforcement is Bazel itself plus a documented convention, not a grep gate).

### Non-goals

- **Sibling one-way rules inside `//src`.** Visibility restricts who may
  depend on a target from *outside* a grant; it cannot express "no reverse
  edge" between two siblings that are both inside `//src:__subpackages__`
  (e.g. `image` must never dep back on `image_processing`, ARCH-MAP.md:47/60;
  the engine never links the shell, ARCH-MAP.md:274). These stay
  `structure`-enforced by dep-set review. Wiring the ad-hoc
  `bazel query somepath(...)` checks into CI is a possible follow-up, decided
  separately — out of scope here.
- **Malice-proofing.** A determined author can re-declare
  `extern "C" cmsSaveProfileToMem`, and — because `layering_check` is
  deferred repo-wide — a TU that transitively reaches `@lcms2` can still
  `#include <lcms2.h>` directly and compile. Pulling `layering_check` into
  scope for the ICC threat model is a separate decision (DEV-6353 deferral
  stands). The bar is preventing accidental dependencies, consistent with
  the rest of the visibility work.
- (The three legacy `test/unit/*` packages — cache, filenamehash, tiff_codecs
  — are IN scope: their narrow module targets all exist already
  (`//src/cache:cache`, `//src/util:util`, `//src/format_handlers`), so
  co-locating them per ADR-0003 is a prerequisite that lets the
  `//src:sipi_lib` test grant shrink to `//test/approval` alone.)

## Alternative Approaches Considered

- **clang-tidy banned-function gate** (`bugprone-unsafe-functions` with
  `CustomFunctions` for `cmsSaveProfileToMem`): rejected — the repo has no
  clang-tidy gate; introducing one for a single function is over-engineering,
  and the header split makes it redundant for the accident case.
- **Grep-based CI gate** for `//visibility:public` under `src/**`: rejected —
  Bazel's own analysis is the enforcement once visibility is tightened; a
  documented convention beats a permanent grep gate.
- **`package_group` for the sipi_lib grant:** rejected — a plain label list is
  shorter, and the grant is expected to shrink (F6), not grow.

## Technical Considerations

Facts the implementation relies on (established during research/review):

1. **Command-line references need no visibility.** `bazel build/run/test
   <label>`, `--platforms=`, `--flaky_test_attempts=<label>@3`, and coverage
   invocations are not gated by visibility — only BUILD-file dependency edges
   are. Making a target private never breaks a justfile recipe or CI step that
   names it directly. (Already documented at `src/iiifparser/fuzz/BUILD.bazel:17`.)
2. **`config_setting` visibility IS enforced**
   (`--incompatible_enforce_config_setting_visibility`, default on in Bazel
   9.1.0; relied on as a default, not pinned in `.bazelrc` — accepted). Two
   instances matter: `//platforms:is_*` (consumed from the `@kakadu` overlay
   AND as `select()` keys in all five `//bazel:llvm-*` aliases,
   bazel/BUILD.bazel — must stay public, see F3) and `//bazel:asan_enabled`
   (sole consumer is `//test/e2e` via `sipi_e2e_test.bzl` macro expansion —
   safe to narrow; `.bzl` macros expand into the instantiating package, so
   visibility is checked against `//test/e2e`).
3. **Overlay visibility labels** must use the canonical repo form
   `@@//pkg:__pkg__` (main repo's canonical name is empty; stable for the root
   module).
4. **F1 — the only overlay-depends-on-overlay edge:** `@jbigkit//:jbig` is
   consumed solely by `@tiff//:tiff_impl` (`bazel/tiff.BUILD.bazel:205`). Both
   are `http_archive` repos, so there is no stable bzlmod spelling for a
   cross-overlay visibility grant. `@jbigkit` stays public with a comment
   naming `@tiff` as the intended sole consumer. Do not attempt the
   `@@//...`-style pattern here.
5. **F2 — `exports_files(["kakadu.BUILD.bazel"])` is NOT vestigial.** It has
   a live label consumer: `bazel/kakadu_extension.bzl:23` (`build_file =
   "//bazel:kakadu.BUILD.bazel"`). The other seven overlays need no export
   only because their `build_file` labels sit at MODULE.bazel level. The
   export stays; its comment gets updated to name the consumer and the
   asymmetry.
6. **F4 — `@sipi_bench_fixtures` is `dev_dependency = True`**: visibility
   tightening is safe for the root module; note in the overlay comment.
7. **Linux-only edges** (`@mimalloc`, `//bazel:glibc23_compat`, docker image
   targets) are not analyzed by a macOS build. The local verification for
   those is an analysis-only cross pass:
   `bazel build --nobuild --platforms=//platforms:linux_aarch64 //...`
   (visibility errors are analysis-phase, so `--nobuild` catches them without
   compiling).
8. **`//src:sipi_lib` full grant** (Finding C — do not drop the src-side
   consumers): the in-`//src` consumers (6 references across `//src/cli`,
   `//src/cli/cpp/commands`, `//src/format_handlers`,
   `//src/image_processing`) are covered by `//src:__subpackages__`; after
   the Phase 2 test co-locations, the only out-of-`//src` consumer left
   is the approval suite, so the final value is
   `["//src:__subpackages__", "//test/approval:__pkg__"]`.
9. **ICC workstream is compile-time only** (verified): the only
   `getIccProfile()` call sites are in `src/image_processing/cpp/color.cpp`
   (:144, :156); no approval-test source touches `cmsHPROFILE`; `//src/image`
   and `//src:sipi_lib` carry `@lcms2` without using any `cms*` symbol
   (`sipi_lib` owns no sources at all — its glob matches nothing; `//src/image`
   needs it header-transitively via `metadata/icc.h`, a need the split removes).
   `Icc::iccBytes()` itself is untouched; goldens cannot move.
10. **The lcms2 surface of `icc.h` is wider than `getIccProfile()`:** three
    public declarations (`icc_error_logger` :31 — also called from
    `color.cpp:109`; `Icc::createFromProfile(cmsHPROFILE&)` :95 — zero call
    sites repo-wide, dead API; `getIccProfile()` :149) plus the private
    `ProfileCloser`/`ProfilePtr` whose inline deleter calls
    `cmsCloseProfile`. Private members cannot move to another header — the
    class itself must become lcms2-free (opaque handle + out-of-line
    deleter). Phase 3 spells out the shape.
11. **`cmsSaveProfileToMem` has one caller outside `icc.cpp`:** the colocated
    test `src/metadata/cpp/icc_parse_test.cpp` (:36, :38), with its own
    `@lcms2` dep (src/metadata/BUILD.bazel:138). The `@lcms2` grant covers
    `//src/metadata`, so it keeps compiling; any "only icc.cpp" phrasing in
    docs or checks must carve out this metadata-internal test.

## Implementation Phases

Four commits: Phase 1; Phase 2's test co-locations; Phase 2's visibility
narrowing; Phase 3 (see PR shape under Dependencies & Risks). Every commit
must leave `just bazel-build` + `just bazel-coverage` green on macOS plus the
`--nobuild` Linux analysis pass (bisectable history under rebase-merge). Run
`just bazel-rustfmt-check` and `just bazel-clippy-check` before any commit
touching Rust-adjacent BUILD files.

#### Phase 1: Vendored-dep overlays (low blast radius, warm-up)

All in `bazel/*.BUILD.bazel`, using `@@//` labels:

- [ ] `@kakadu`: package default → `["@@//src:__subpackages__"]`; make the
      internal `coresys*`, `kdu_aux*`, `kdu_ht` sub-targets private so only
      `:kdu` is exported (the file says so itself at kakadu.BUILD.bazel:353)
- [ ] `@exiv2`: `:exiv2` → `["@@//src:__subpackages__"]`; generated-header
      rules `:exv_conf_h`, `:exiv2lib_export_h` → private
- [ ] `@tiff`: `:tiff` → `["@@//src:__subpackages__", "@@//test:__subpackages__"]`
      (interim — Phase 2 narrows the test side to `//test/unit/fixtures` once
      the tiff_codecs migration lands)
- [ ] `@tiff`: delete the dead `:libtiff` alias (no consumers)
- [ ] `@jansson`: package default → `["@@//src:__subpackages__"]`; generated
      config-header rules → private
- [ ] `@tracy`: → `["@@//src/observability:__pkg__"]`
- [ ] `@mimalloc`: → `["@@//src/cli/rust:__pkg__"]` (turns the
      mimalloc.BUILD.bazel:17-20 comment into an invariant)
- [ ] `@sipi_bench_fixtures`: `:images` → `["@@//src/format_handlers:__pkg__"]`;
      fix the stale docstring at benchmark_fixtures.BUILD.bazel:7 ("in `//src`"
      → the benchmarks live in `//src/format_handlers` per ADR-0003); note in
      the same comment that the repo is `dev_dependency = True` (F4)
- [ ] `@jbigkit`: stays public; add a comment that its sole intended consumer
      is `@tiff//:tiff_impl` and why no visibility grant can express that (F1)
- [ ] Negative check: add a scratch dep on `@mimalloc//:mimalloc` from an
      engine target, confirm the analysis-phase visibility error, revert

#### Phase 2: Main-repo packages and test tree (highest blast radius)

Order inside the commit matters only for local verification; the package-default
flip on `//src` is done after all grants exist.

Test co-location prerequisites (ADR-0003; each unlocks a grant reduction):

- [ ] Move `test/unit/cache/cache.cpp` to a colocated `cc_test` in
      `//src/cache` linking `//src/cache:cache` (+ `//src/error`), gtest_main
      instead of `main.cpp`; delete `test/unit/cache/` (mirrors `util_test`,
      ARCH-MAP.md:177)
- [ ] Move `test/unit/filenamehash/sipifilenamehash.cpp` into `//src/util`'s
      colocated test (links `:util`; `SipiFilenameHash` is a util citizen —
      src/util/BUILD.bazel:3); delete `test/unit/filenamehash/`
- [ ] Move `test/unit/tiff_codecs/malformed_tiff_test.cpp` into
      `//src/format_handlers:formats_test` `srcs` (its docstring already
      claims the malformed-input error-path territory; data/env plumbing
      already present)
- [ ] Move `test/unit/tiff_codecs/tiff_codecs_test.cpp` (raw libtiff
      codec-availability proof, DEV-6563) to a colocated `cc_test` in
      `//src/format_handlers` linking only `@tiff//:tiff` + gtest_main;
      delete `test/unit/tiff_codecs/`

`//src` (src/BUILD.bazel):

- [ ] `:sipi_lib` → `["//src:__subpackages__", "//test/approval:__pkg__"]`
- [ ] Narrow `@tiff` further (from the Phase 1 interim value) to
      `["@@//src:__subpackages__", "@@//test/unit/fixtures:__pkg__"]` — after
      the tiff_codecs migration, the fixture generator is the only test-tree
      consumer left
- [ ] `:image_load` → `["//test/e2e:__pkg__"]`
- [ ] Flip `package(default_visibility)` from public to
      `["//visibility:private"]` — final step; everything else in the package
      (version/config/ICC-header genrules, image layers, push/load plumbing,
      debug-split) is consumed in-package or from the command line only
- [ ] `//src/cli`: `:sipi_report` → `["//src/cli:__subpackages__"]`, `:cli_app`
      → `["//src/cli/rust:__pkg__"]`, `:sipi` (cc_binary) →
      `["//test/e2e:__pkg__"]`; package default → private
- [ ] `//src/cli/rust`: `:sipi` (rust_binary) → `["//src:__pkg__",
      "//test/e2e:__pkg__"]`; `:mi_stats_shim`, `:linux_sans_asan`, unit test →
      private (package default → private)
- [ ] `//src/server/rust`: package default → `["//src/cli/rust:__pkg__"]`
- [ ] `//src/image_processing`: package default → `["//src:__subpackages__"]`
      with per-target `//test/approval:__pkg__` grant on the library (mirrors
      classifier precedent); `:process_benchmark`, `:image_processing_test` →
      private; rewrite the docstring at image_processing/BUILD.bazel:25-31
- [ ] `//src/format_handlers/corpus/{j2k,jpeg,png,tiff}`: →
      `["//src/format_handlers:__subpackages__"]`
- [ ] `//src/iiifparser/corpus`: → `["//src/iiifparser:__subpackages__"]`

Root, `bazel/`, and support packages:

- [ ] Root `:lsan_suppressions` → `["//test:__subpackages__"]` (keeps the
      documented headroom, BUILD.bazel:11-12)
- [ ] Root `:test_fixtures` → `["//test/e2e:__pkg__"]`
- [ ] `//bazel`: `:llvm-objcopy`, `:llvm-readelf` → `["//src:__pkg__"]`;
      `:llvm-symbolizer`, `:asan_enabled` → `["//test/e2e:__pkg__"]`;
      `:glibc23_compat` → `["//src:__subpackages__"]`; fix its stale consumer
      comment (bazel/BUILD.bazel:79 — also linked by format_handlers and image)
- [ ] `//bazel`: `:llvm-cov`, `:llvm-profdata` stay public with a
      command-line-only comment (the coverage recipes `bazel build` them and
      locate the binaries via `bazel cquery --output=files`, justfile:99-102 —
      no BUILD-file label deps)
- [ ] `//bazel`: keep `exports_files(["kakadu.BUILD.bazel"])` (live consumer:
      `kakadu_extension.bzl:23`, F2); update its comment to name the consumer
      and why only kakadu needs the export (the other overlays' `build_file`
      labels sit at MODULE.bazel level)
- [ ] `//tools`: `:bin2c` → `["//src:__pkg__", "//src/util:__pkg__"]` (its
      consumers: `src/BUILD.bazel:98` and the `magic_database.bzl` macro
      expanding into `//src/util`); `exports_files(["workspace_status.sh"])`
      stays public-by-default with its existing future-intent comment — a
      documented exception
- [ ] `//config`: → `["//:__pkg__", "//src:__subpackages__"]`; fix stale
      docstring naming nonexistent `//test/unit/configuration` (config/BUILD.bazel:5)
- [ ] `//include`: `exports_files` visibility → `["//src:__pkg__"]`
- [ ] `//server`: → `["//:__pkg__", "//src:__pkg__"]`
- [ ] `//scripts`: → `["//:__pkg__", "//src:__subpackages__"]`
- [ ] `//platforms`: `platform()` targets → private (command-line only);
      `config_setting()`s stay public with a comment naming both consumers
      that force it: the `@kakadu` reverse reference and the `select()` keys
      in the five `//bazel:llvm-*` aliases (F3)
- [ ] `//test`: `:test_paths` → `["//src:__subpackages__", "//test:__subpackages__"]`
- [ ] `//test/_test_data`: → `["//:__pkg__", "//src:__subpackages__",
      "//test:__subpackages__"]`
- [ ] `//test/approval`: `:approved_goldens` → private
- [ ] `//test/e2e`: drop all four `//visibility:public` exceptions
      (`:snapshots`, `:sipi_e2e`, `:sipi_image_tar`, `:all_e2e`) — all
      in-package or command-line only
Docs sync (same commit as the change it documents):

- [ ] ARCH-MAP.md:55 — replace the image_processing public-visibility claim
- [ ] ARCH-MAP.md:266 — full rewrite, the line is stale beyond this change:
      `memory_budget` and `logger` are already colocated narrow tests,
      `configuration` was ported to `//src/scripting/rust:config_parse_test`,
      and `filenamehash` is missing; after the co-locations the `sipi_lib`
      bucket is `//test/approval` only
- [ ] CONVENTIONS.md — add the repo-wide visibility policy: default is
      `//src:__subpackages__` for engine modules, private for everything else;
      grants are explicit; overlay files use `@@//` labels; command-line
      references need no visibility
- [ ] Negative check: scratch dep on `//src:image_load` from a `//src`
      subpackage BUILD file, confirm visibility error, revert

#### Phase 3: ICC chokepoint enforcement (independent of Phases 1–2)

Strict order inside the commit: header split first, `@lcms2` narrowing last.
The lcms2 surface of `icc.h` is three public declarations plus the private
deleter (Technical Consideration #10); private members cannot move, so the
class itself goes lcms2-free:

- [ ] Make `icc.h` lcms2-free: drop the `<lcms2.h>` include; `ProfileCloser`
      keeps only a declared `void operator()(void *) const` (definition moves
      out-of-line to icc.cpp, which includes `<lcms2.h>` directly);
      `ProfilePtr` becomes `std::unique_ptr<void, ProfileCloser>`; replace
      `cmsHPROFILE getIccProfile() const` with an opaque
      `void *profileHandle() const`; move the `icc_error_logger` extern
      declaration out (next item)
- [ ] Delete `Icc::createFromProfile(cmsHPROFILE&)` — dead API, zero call
      sites repo-wide
- [ ] New header `src/metadata/cpp/internal/icc_lcms2.h`: the
      `icc_error_logger` extern declaration plus an inline
      `cmsHPROFILE`-typed wrapper over `profileHandle()`; new header-only
      `cc_library(name = "icc_lcms2")` in
      `src/metadata/cpp/internal/BUILD.bazel` with
      `deps = ["//src/metadata", "@lcms2//:lcms2"]` (no cycle: metadata does
      not dep back on it; icc.cpp declares/defines its own symbols and does
      not include this header)
- [ ] Per-target visibility on `:icc_lcms2` =
      `["//src/metadata:__pkg__", "//src/image_processing:__pkg__"]` — the
      package default stays `//src/metadata:__pkg__` so the internal
      package's Test-seam charter is unchanged; the target docstring states
      the one production grant (color.cpp needs typed handles for
      `cmsCreateTransform`)
- [ ] Update `src/image_processing/cpp/color.cpp` to include
      `metadata/internal/icc_lcms2.h` and use the typed wrapper at :144/:156;
      add `//src/metadata/cpp/internal:icc_lcms2` to
      `//src/image_processing`'s deps
- [ ] Drop `@lcms2` from `//src:sipi_lib` (src/BUILD.bazel:210) and
      `//src/image` (src/image/BUILD.bazel:72) — header-transitive only today,
      and the split removes `//src/image`'s transitive need
- [ ] Narrow `@lcms2` (bazel/lcms2.BUILD.bazel) to
      `["@@//src/metadata:__pkg__", "@@//src/metadata/cpp/internal:__pkg__",
      "@@//src/image_processing:__pkg__"]` (the internal package is its own
      package — a `//src/metadata:__pkg__` grant does not cover it)
- [ ] Negative check: add a scratch `@lcms2//:lcms2` dep to
      `//src/format_handlers`, confirm the analysis-phase visibility error,
      revert
- [ ] Update the CLAUDE.md ICC-invariant paragraph: the chokepoint is now
      structurally enforced (state how, including the layering_check residue
      and the `icc_parse_test.cpp` carve-out for `cmsSaveProfileToMem`), keep
      the SOURCE_DATE_EPOCH half
- [ ] Verify `//src/metadata:icc_parse_test` still compiles and passes (it
      calls `cmsSaveProfileToMem` with its own `@lcms2` dep — covered by the
      grant)
- [ ] Verify approval tests byte-identical (`just bazel-test-approval`) — the
      refactor must not move a single golden

## Acceptance Criteria

- [ ] No `//visibility:public` remains in the repo except the documented
      exceptions: `//platforms` config_settings (F3), `@jbigkit` (F1),
      `//bazel:llvm-cov`/`:llvm-profdata` (command-line only), and
      `//tools`'s `exports_files(["workspace_status.sh"])` (public-by-default,
      future-intent comment), each carrying a comment naming its reason
- [ ] Each of the three negative checks (Phase 1 `@mimalloc`, Phase 2
      `//src:image_load`, Phase 3 `@lcms2`) produced a real analysis-phase
      visibility error (proves tightening happened, not just that nothing broke)
- [ ] `just bazel-build`, `just bazel-coverage`, `just bazel-test-approval`
      green on macOS after every commit
- [ ] `bazel build --nobuild --platforms=//platforms:linux_aarch64 //...`
      passes locally (covers @mimalloc / glibc23_compat / image-target edges
      before CI)
- [ ] `just bazel-rustfmt-check` and `just bazel-clippy-check` green
- [ ] CI matrix green on all three platforms
- [ ] No approval golden changed; no runtime behavior change
- [ ] `test/unit/cache/`, `test/unit/filenamehash/`, `test/unit/tiff_codecs/`
      no longer exist; their tests are colocated with their modules linking
      narrow targets (`//src/cache:cache`, `//src/util:util`,
      `//src/format_handlers` / `@tiff`)
- [ ] `//src:sipi_lib`'s only out-of-`//src` consumer is `//test/approval`
- [ ] ARCH-MAP.md, CONVENTIONS.md, CLAUDE.md, and all stale BUILD comments
      updated in the same commits as their code changes

## Dependencies & Risks

- **PR shape:** one branch, one PR, four commits — Phase 1; the test
  co-locations (a `test(...)` commit, different kind of change than the
  visibility edits); the Phase 2 visibility narrowing; Phase 3. Each is an
  independent, self-contained change (the rebase-merge multi-commit
  criterion). Scopes: `build(bazel)` for the two visibility commits (`bazel`
  is a documented cross-cutting scope), `test(cache,util,format_handlers)`
  for the co-locations, `refactor(metadata)` for Phase 3.
- **Missed dynamic consumer:** a genquery/aspect or `.bzl` macro expanding a
  label in an unexpected package would surface as an analysis error in the
  per-commit builds — the Linux `--nobuild` pass is mandatory precisely because
  macOS analysis skips Linux-only edges.
- **`@kakadu` fetch requirement:** builds need `gh auth login` +
  `dasch-swiss` membership; nothing new, but the negative checks require a
  fetched repo.

## Success Metrics

- Every documented "consumed only by" comment in a BUILD file is either an
  enforced visibility declaration or carries an explicit reason why it cannot
  be (F1/F3).
- A new format handler that tries to serialize an ICC profile without
  `Icc::iccBytes()` fails at compile time (no typed profile handle reachable
  through `icc.h`) or at analysis time (adding the `@lcms2` dep is a
  visibility error) — instead of at approval-test time. Residue: a textual
  `#include <lcms2.h>` against transitively-reachable headers still compiles
  until `layering_check` is adopted (accepted, see Non-goals).

## References

- Consumer map and audit: research session 2026-08-30 (this folder)
- Patterns to reuse: `src/metadata/cpp/internal/BUILD.bazel:23`,
  `src/cli/cpp/commands/BUILD.bazel:29`,
  `src/iiifparser/cpp/classifier/BUILD.bazel:29`,
  `src/iiifparser/fuzz/BUILD.bazel:14-18`, `test/e2e/BUILD.bazel:68`
- Overlay canonical-label precedent: `bazel/kakadu.BUILD.bazel:35-37`
- Boundary rules: `ARCH-MAP.md:274` (one-way top-level), `:277` (Test seam),
  `:266` (sipi_lib test bucket)
- ADRs: `docs/adr/0003-module-co-located-source-and-tests.md` (visibility as
  build invariant), `docs/adr/0002-icc-profile-determinism-test-only.md`,
  `docs/adr/0013-shttps-as-internal-module.md` (allowlist pattern),
  `docs/adr/0019-mimalloc-production-allocator.md`
