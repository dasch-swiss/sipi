# Execution journal — Bazel visibility tightening (DEV-7083)

Plan: `01-refactor-bazel-visibility-tightening-plan.md`
Target repo: `dasch-swiss/sipi` (worktree `bazel-visibility-tightening`)
Branch: `feature/dev-7083-bazel-visibility-plan`
Base commit: `81029b49` (plan commit; implementation commits follow it)

Commit shape is fixed by the plan: four commits, in order.

| # | subject | phase |
|---|---------|-------|
| 1 | `build(bazel): …` overlay visibility | Phase 1 |
| 2 | `test(cache,util,format_handlers): …` test co-locations | Phase 2 prereq |
| 3 | `build(bazel): …` main-repo visibility + docs | Phase 2 |
| 4 | `refactor(metadata): …` ICC chokepoint | Phase 3 |

## Chunk table

| id | commit | status | sha | summary |
|----|--------|--------|-----|---------|
| C1.1 | 1 | done | 96b5d24d | `@kakadu`/`@exiv2`/`@tiff`/`@jansson` overlay narrowing + drop dead `:libtiff` alias |
| C1.2 | 1 | done | 96b5d24d | `@tracy`/`@mimalloc`/`@sipi_bench_fixtures`/`@jbigkit` overlay narrowing + comments + Phase 1 negative check |
| C2.1 | 2 | done | 478c68da | co-locate `test/unit/cache` into `//src/cache:cache_test` (16/16 cases preserved) |
| C2.2 | 2 | done | 478c68da | fold `test/unit/filenamehash` into `//src/util:util_test` (39 -> 49 cases) |
| C2.3 | 2 | done | 478c68da | split `test/unit/tiff_codecs` into `formats_test` + `//src/format_handlers:tiff_codecs_test` (7/7 cases preserved); narrowed the `@tiff` grant |
| C2.4 | 2 | done | 478c68da | scrub stale `test/unit/{cache,filenamehash,tiff_codecs}` references from live docs |
| C3.1 | 3 | done | bd2f1c18 | `//src` package: `sipi_lib`, `image_load`, default private |
| C3.2 | 3 | done | bd2f1c18 | `//src/cli`, `//src/cli/rust`, `//src/server/rust` |
| C3.3 | 3 | done | bd2f1c18 | `//src/image_processing` + the five corpus packages |
| C3.4 | 3 | done | bd2f1c18 | root, `//bazel`, `//tools`, `//config`, `//include`, `//server`, `//scripts`, `//platforms` |
| C3.5 | 3 | done | bd2f1c18 | `//test`, `//test/_test_data`, `//test/approval`, `//test/e2e` + Phase 2 negative check |
| C3.6 | 3 | done | bd2f1c18 | docs sync: ARCH-MAP.md:55/:266, CONVENTIONS.md visibility policy, developing.md test-layout guidance |
| C3.7 | 3 | done | bd2f1c18 | audit gap: inline `@ffmpeg_static_linux_*` overlays + `//bazel/patches` (not enumerated by the plan) |
| C4.1 | 4 | done | 6205526a | `icc.h` lcms2-free split + `internal/icc_lcms2.h` |
| C4.2 | 4 | done | 6205526a | `color.cpp` rewire + `@lcms2` dep drops + overlay narrowing + Phase 3 negative check; returned `blocked` on a latent `FALSE` macro leak |
| C4.2b | 4 | done | 6205526a | resolve the `FALSE` leak: `SipiIOPng.cpp:239,246` -> `false` |
| C4.3 | 4 | done | 6205526a | CLAUDE.md ICC-invariant paragraph |

## Negative-check evidence

(captured per phase; see Acceptance Criteria bullet 2)

### Phase 1 — `@mimalloc` (commit 96b5d24d)

Scratch dep on `@mimalloc//:mimalloc` added to `//src/format_handlers`, then
reverted. `bazel build --nobuild --platforms=//platforms:linux_x86_64
//src/format_handlers:format_handlers` produced:

```
ERROR: .../BUILD.bazel:45:11: in cc_library rule //src/format_handlers:format_handlers:
Visibility error: target '@@+http_archive+mimalloc//:mimalloc' is not visible from
target '//src/format_handlers:format_handlers'
Recommendation: modify the visibility declaration if you think the dependency is legitimate.
```

### Phase 2 — `//src:image_load` (commit bd2f1c18)

Scratch `//src:image_load` data dep added to `//src/format_handlers:formats_test`,
then reverted:

```
ERROR: .../src/format_handlers/BUILD.bazel:135:8: in cc_test rule //src/format_handlers:formats_test:
Visibility error: target '//src:image_load' is not visible from
target '//src/format_handlers:formats_test'
Recommendation: modify the visibility declaration if you think the dependency is legitimate.
For more info see https://bazel.build/concepts/visibility
```

Note: on the macOS host platform the incompatible-target check short-circuits
*before* visibility checking, masking the error. `image_load` is Linux-only, so
the negative check only bites under
`--platforms=//platforms:linux_aarch64 --extra_execution_platforms=//platforms:linux_aarch64`.

### Phase 3 — `@lcms2` (commit 6205526a)

Scratch `@lcms2//:lcms2` dep added to `//src/format_handlers`, then reverted.
Reproduced on the macOS host platform directly (no cross-platform flags needed):

```
ERROR: src/format_handlers/BUILD.bazel:45:11: in cc_library rule //src/format_handlers:format_handlers:
Visibility error: target '@@+http_archive+lcms2//:lcms2' is not visible from
target '//src/format_handlers:format_handlers'
Recommendation: modify the visibility declaration if you think the dependency is legitimate.
For more info see https://bazel.build/concepts/visibility
```

All three negative checks therefore produced real analysis-phase visibility
errors, satisfying Acceptance Criterion 2.

## Deviations from the plan's commit assignment

- The plan assigns the final `@tiff` narrowing (interim -> `//test/unit/fixtures`)
  to commit 3. It landed in **commit 2** instead. Reason: the narrowing is a
  direct consequence of the tiff_codecs co-location, and the `:tiff` target's
  consumer comment is only accurate once both land together. Splitting them
  would leave commit 2 carrying a knowingly-stale comment, which breaks the
  bisectable-history intent more than the scope-name mismatch does.
- Commit 2 also carries a live-docs scrub (C2.4) that the plan did not
  enumerate: `CONVENTIONS.md`, `docs/adr/0015`, `docs/src/development/bazel.md`,
  `docs/src/development/developing.md` all named the three deleted
  `test/unit/*` directories. Per the same-commit docs rule, they belong here.
- Commit 3 carries two `//visibility:public` sites the plan's audit missed
  (C3.7), found by a post-chunk repo-wide grep. Both are required by the
  plan's own first acceptance criterion, so they landed in the Phase 2
  commit rather than being deferred:
  - the inline `build_file_content` overlays for
    `@ffmpeg_static_linux_{amd64,arm64}` in `MODULE.bazel` — overlays spelled
    inline rather than as `bazel/*.BUILD.bazel` files, which is why the
    original grep missed them. Narrowed to `@@//src:__pkg__`.
  - `//bazel/patches` `exports_files` — narrowed to `//:__pkg__`.
    Empirically confirmed that `single_version_override(patches = …)` label
    resolution IS subject to package visibility, so a root-package grant is
    both sufficient and necessary.
  Commit 3 therefore also touches `MODULE.bazel.lock` (one
  `bzlTransitiveDigest` line), which must move with `MODULE.bazel`.

## Deferrals

- `docs/adr/0015-native-cc_library-over-foreign_cc.md` line ~104 references
  `//test/unit/sentry_smoke`, which no longer exists. Pre-existing staleness,
  unrelated to this plan. Not fixed.
- `docs/src/development/developing.md` (~lines 135-139) still instructs adding
  new unit tests under `test/unit/<mod>/BUILD.bazel` and describes CI unit
  coverage as `//test/unit/...`-scoped. That guidance predates ADR-0003
  co-location and is now the opposite of the repo's default. Folded into the
  commit-3 docs chunk (C3.6) alongside the CONVENTIONS.md policy write-up.

## Side findings

### The ICC split exposed a latent `FALSE` macro leak in SipiIOPng

`src/format_handlers/cpp/SipiIOPng.cpp:239` and `:246` read `return FALSE;`.
`FALSE` is not a libpng symbol — it is `#define FALSE 0` from `<lcms2.h>`,
which had been reaching that translation unit transitively via
`SipiImage.h` -> `metadata/icc.h`. Making `icc.h` lcms2-free removed the leak
and broke the build. Fixed to the C++ keyword `false` (behaviour-preserving:
the enclosing function returns `bool` and `FALSE` expanded to `0`). Folded into
commit 4, the commit that removes the leak, rather than a standalone `fix:`.

A full `bazel build //...` sweep confirmed this was the ONLY symbol in the tree
relying on the old leak. Note `TRUE`/`FALSE` in `SipiIOJpeg.cpp` are libjpeg's
own `boolean` API constants supplied by libjpeg's headers — correct as-is.

This is the kind of latent defect the visibility/header tightening exists to
surface, and is worth remembering: `layering_check` being deferred repo-wide
(DEV-6353) means more of these are likely still hiding.

### `just bazel-coverage` is not a usable local gate on macOS

32 of 74 targets "fail" under `just bazel-coverage`, but every failure is in the
coverage-collection script, not the test:

```
error: .../_coverage/src/util/checked_arith_test/test/_cc_coverage.profdata: No such file or directory
error: coverage collection script failed
```

The GoogleTest bodies themselves report `[  PASSED  ]` in the same log. This is
the documented, pre-existing `rules_cc` coverage gap (coverage is a stretch
metric for SIPI, not a gate) and is unrelated to any change in this plan.
**Substituted gate:** `just bazel-test` — same target set
(`//src/... //test/unit/... //test/approval/... //test/e2e/...`), no
instrumentation. 74/74 pass. Used for every commit in this plan.

### The Linux analysis pass needs an extra execution platform locally

The plan's `bazel build --nobuild --platforms=//platforms:linux_aarch64 //...`
fails on a pristine tree with `No matching toolchains found` for the fuzz/test
targets (e.g. `//src/format_handlers/fuzz:j2k_decode_fuzz`) — macOS has no
execution platform for non-host test targets without the RBE flags CI injects.
Adding `--extra_execution_platforms=//platforms:linux_aarch64` resolves it and
the pass completes clean. Full local incantation used for every commit:

```
bazel build --nobuild --platforms=//platforms:linux_aarch64 \
    --extra_execution_platforms=//platforms:linux_aarch64 //...
```

`--extra_execution_platforms` is a command-line reference, so it keeps working
after Phase 2 makes the `platform()` targets private.

## Review-fix round

Adversarial review of the four-commit stack produced seven verified findings.
All were fixed and **folded into their introducing commits** (`git commit
--fixup` + `GIT_SEQUENCE_EDITOR=: git rebase -i --autosquash --autostash`), per
the repo's "a bug introduced earlier in the same branch is folded into its
introducing commit" rule. The stack is still exactly four commits; the two
pre-fold SHAs `bd2f1c18`/`6205526a` became `41a762a2`/`7ba78840`.

| # | finding | folded into |
|---|---------|-------------|
| 1 | `@ffmpeg_static_linux_{amd64,arm64}` overlays: the narrowed `:binaries` filegroup was **dead** (zero consumers) while the actually-consumed `exports_files` file labels carried no `visibility` argument and so defaulted to PUBLIC — the narrowing never took effect. Filegroup deleted; `visibility = ["@@//src:__pkg__"]` moved onto `exports_files`. | commit 3 |
| 2 | `bazel/approvaltests_cpp.bzl`: generated `cc_library` was `//visibility:public`. Narrowed to `@@//test/approval:__pkg__` (canonical `@@//` form — the content is repository-rule-generated) + sole-consumer comment. | commit 3 |
| 3 | `CONVENTIONS.md`: deleted the orphaned "residual historical-layout test targets" paragraph — its antecedent list was removed by this same commit. | commit 3 |
| 4 | `justfile`: `bazel-test-unit`'s "both the legacy `//test/unit/<x>/` AND co-located" comment was false post-co-location. Rewritten; target patterns unchanged (`//test/unit/...` still matches the fixtures package). | commit 3 |
| 5 | `ARCH-MAP.md`: record the `icc_lcms2` per-target-grant exception (Local-context kit, metadata boundary rule, Test-seam convention). | commit **4** (see deviation) |
| 6 | `src/metadata/cpp/internal/BUILD.bazel` docstring: the blanket "Visibility is restricted to `//src/metadata:__pkg__`" claim is inaccurate package-wide now that `:icc_lcms2` carries an extra grant. | commit 4 |
| 7 | `src/metadata/cpp/icc.h` docstring: "makes heavy use of the functions provided by the littleCMS2 library" contradicted the new invariant. Corrected to state the header is deliberately lcms2-free. | commit 4 |

### Deviation: finding 5 folded into commit 4, not commit 3

The fix brief assigned the `ARCH-MAP.md` edits to commit 3. They document
`:icc_lcms2`, which **commit 4 introduces** — verified: `git show
bd2f1c18:src/metadata/cpp/internal/BUILD.bazel | grep -c icc_lcms2` returns 0.
Folding them into commit 3 would have made commit 3 describe a target that does
not exist in its own tree, breaking the bisectable-history intent the same way
the C2.3 deviation above did. Folded into commit 4 instead; confirmed commit 3's
tree carries zero `icc_lcms2` references. Findings 3 and 4 stayed in commit 3 as
briefed (finding 3's antecedent is removed by commit 3 itself).

### Refuted / dismissed review items

- **"the `//test/unit/fixtures` targets are implicitly public"** — refuted. Rule
  targets default to **private**, not public, absent a `package(default_visibility
  = …)`; there is no gap to close.
- **"add a grep-based CI gate against `//visibility:public`"** — dismissed. The
  plan's Alternative Approaches section already considered and rejected a bespoke
  grep gate; Bazel's own analysis-phase visibility check is the enforcement
  mechanism, and a one-time migration does not earn a permanent gate.

### Gates after the fold

Run on the folded tree (verified byte-identical to the gated working tree via
`git hash-object` on all seven files before and after the rebase):

- `just bazel-build` — pass
- `just bazel-test` — 73/73 pass
- `just bazel-rustfmt-check` — pass
- `just bazel-clippy-check` — pass
- `bazel build --nobuild --platforms=//platforms:linux_aarch64
  --extra_execution_platforms=//platforms:linux_aarch64 //...` — 188 targets
  analyzed clean. This is the pass that exercises the Linux-only ffmpeg edge;
  `//src` still resolves `@ffmpeg_static_linux_*//:bin/{ffmpeg,ffprobe}` through
  the narrowed `exports_files`.

`MODULE.bazel.lock` did **not** change — the lock does not digest
`build_file_content`, so finding 1 needed no lock update.

### Correction to an earlier journal entry

The `//src:image_layers_{amd64,arm64}` target names used in this journal's
Phase 2 notes do not exist. The real ffmpeg-consuming targets are
`//src:ffmpeg_layer_amd64` / `//src:ffmpeg_layer_arm64`, selected into
`//src:sipi_image`.
