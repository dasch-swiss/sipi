---
title: "feat: codec fuzz harness + value-based error migration roadmap"
type: feat
date: 2026-08-28
author: "Ivan Subotic"
status: reviewed
linear: DEV-7066, DEV-7056
---

# feat: codec fuzz harness + value-based error migration roadmap

## Enhancement Summary

**Deepened on:** 2026-08-28. Five research passes: cpp-patterns skill application,
fuzz-harness specifics (local precedent + external), external `std::expected`
migration practice, dune topology review of the Phase 2 layout, and
institutional-learnings triage (dasch-specs). Key additions: concrete libFuzzer
flag values + AFL++ dictionaries + a scoped `detect_leaks` decision (Phase 1);
the `read_watermark` boundary directionality fix and `bazel query` acceptance
checks for the new packages (Phase 2, from the dune review); an illustrative
`ErrorCode`/`ErrorPolicy` sketch, the setjmp→`Result` bridge pattern, a PNG
throw-from-callback alternative for the ADR, and separate benchmark scrutiny on
the Phase 5e vtable flip (Phases 3–8). External research validated (not
changed): single canonical error type, boundary-complete-per-module staging,
the intermediate-only adapter. All sketches are ILLUSTRATIVE — the Phase 3 ADR
decides final shapes.

## Overview

Combined roadmap for two related initiatives over the C++ image layer:

1. **DEV-7066** — a native C++ libFuzzer harness (`cc_fuzz_test`) over the codec
   decode entry points (`SipiIO{Tiff,Jpeg,Png,J2k}::read` / `::read_shape`),
   reusing the existing `--config=fuzz` / `fuzz.yml` / two-tier corpus machinery
   built for the IIIF-parser harness.
2. **DEV-7056** — the phased migration of the image/codec layer from
   exception-based error signalling (`throw SipiImageError`) to value-based
   errors (`std::expected`), completing cpp-style-guide §3.14, boundary-complete
   one module at a time.

**Delivery model: one branch, one PR, all-or-nothing.** All phases ship
together; the phase order below is functional (what must exist before what),
not a schedule. Commit structure on the branch follows the commit conventions
(self-contained commits, cleaned up before merge).

**Ordering decision: fuzzer first.** The fuzzer is exactly the safety net the
~80-throw-site migration needs while its commits are being developed — every
handler refactor is fuzzed on the branch before the PR ships. The migration
then proceeds in its sub-issue order (ADR → foundation → handlers → hub →
metadata → seams), with the harness's try/catch shrinking as each handler
migrates.

## Problem Statement / Motivation

The DEV-6418 adversarial review found two Critical memory-corruption bugs by
hand (`parse_photoshop` pointer-overshoot heap over-read; J2K 8-bit
decode-buffer overflow via `INT_MAX` dimension truncation) that no unit test
would have caught — they require crafted inputs fuzzing finds in minutes. The
structural detector for this bug class does not exist for the codecs.

Separately, the image layer predates the §3.14 convention (`std::expected` for
fallible ops) and still throws from ~80 sites across `SipiImage.cpp`, the four
format handlers, and the metadata parsers. The failure contract is invisible at
call sites; the FFI seams bound the damage but the legibility and error-path
cost remain.

## Proposed Solution

Eight phases, all in one PR. Phase 1 (fuzzer) is independent and comes first
in the commit order. Phase 2 executes ADR-0007's structural shape: the hub
dissolves into `src/image/` + `src/image_processing/` + `src/cache/` +
`src/error/` (no
`engine/` grab-bag folder — "engine" stays umbrella vocabulary per ARCH-MAP),
and every package folder adopts `cpp/` / `rust/` source subfolders (no `-rs`
folder suffixes) — so all subsequent work lands in final paths. Phases 3–8 are
the DEV-7056 sub-issues in dependency order, informed by research findings that
correct several assumptions in the issue texts (see Technical Considerations).

## Alternative Approaches Considered

- **Migration first, fuzzer second** — rejected: refactoring every codec error
  path without the structural detector in place inverts the risk.
- **Interleave per handler (fuzz TIFF → migrate TIFF → …)** — rejected: more
  coordination for no gain; one harness covers all codecs from day one.
- **One combined fuzz target dispatching on magic bytes** — rejected in favor of
  **per-format `cc_fuzz_test` targets**: format-focused corpora converge faster,
  crashes triage trivially to a handler, and libFuzzer's coverage signal is not
  diluted across four decoders.
- **BCR-only harness excluding Kakadu from the build** — rejected:
  `//src/formats:formats` is one monolithic `cc_library` with an unconditional
  `@kakadu//:kdu` dep (`src/formats/BUILD.bazel:68`), so every fuzz binary links
  Kakadu regardless. DEV-7066's "BCR first, Kakadu deferred" therefore reduces
  to a corpus/fuzz-time decision, not a build split. All four targets land
  together; splitting `:formats` into per-codec libraries is an explicit
  non-goal of Phase 1.
- **Blocking the hub migration on ADR-0007 as written** — rejected: ADR-0007
  (status: proposed) is partly stale (see below) and must be reconciled by the
  DEV-7057 ADR before the hub phase, rather than treated as a fixed
  prerequisite checklist.

## Technical Considerations

### Fuzz harness mechanics (Phase 1)

- **File-based entry.** No memory overload exists anywhere on `SipiIO`; the
  harness calls the 2-arg convenience `read` overload (`src/SipiIO.h:172-175`)
  and `read_shape` (`src/SipiIO.h:206`), both taking a filepath. The
  harness writes each mutated input to a temp file per iteration (one fixed
  path per process, truncate+rewrite — libFuzzer runs single-threaded per
  process).
- **Direct handler instantiation.** All four handlers are default-constructible
  (matching `src/formats/format_registry.cpp:23-28`); the harness instantiates
  the concrete handler, bypassing extension sniffing. `SipiImage` default-
  constructs.
- **Expected-reject vs finding.** Codecs currently throw `Sipi::SipiImageError`
  on malformed input; the harness catches it (and `std::exception`) as the
  benign "rejected cleanly" outcome. Findings are what escapes: SIGSEGV,
  SIGABRT, ASan/UBSan reports. `validate_decode_dims` (`src/SipiIO.h:52-63`)
  already caps dimensions (`1<<17`) and channels (32) before large allocations,
  bounding OOM-by-header inputs.
- **No memory-budget wiring needed.** `SipiMemoryBudget` is applied only at the
  serve seam (`src/ffi/serve_image.cpp:620-674`), not in the codec layer — the
  harness calls handlers directly, as the unit tests do.
- **Two modes, one target.** `cc_fuzz_test`'s default replay engine makes
  `bazel test //src/...` a corpus-replay regression test on every platform;
  `--config=fuzz` flips to the libFuzzer mutation engine (`.bazelrc:318-389`).
  `rules_fuzzing` 0.8.0 is already a dev `bazel_dep` (`MODULE.bazel:37`).
- **CI credential change.** `fuzz.yml` deliberately omits `DASCHBOT_PAT` today
  because the parser target has no Kakadu edge (`.github/workflows/fuzz.yml:122-132`).
  The codec targets introduce that edge, so the workflow's Bazel steps gain
  `GH_TOKEN: ${{ secrets.DASCHBOT_PAT }}` (mirroring `ci.yml`).
- **Corpus seeds are Git LFS.** `test/_test_data/images/malformed/` (9 fixtures,
  including `jpeg_photoshop_overshoot_app13.jpg`) plus small valid fixtures from
  `test/_test_data/images/unit/`. Fresh worktrees need `git lfs pull`. The
  crafted-fixture split is uneven — TIFF 4, JPEG 4, J2K 1, **PNG 0** — so the
  PNG target starts from valid seeds only; mutation fuzzing does not require
  crafted seeds, but the asymmetry is deliberate, not an oversight.
- **Known coverage gap to close.** The J2K dimension-overflow fix (RF2,
  `SipiIOJ2k.cpp:698-782` via `checked_buf_size`) has **no regression fixture**;
  the harness should reproduce and pin it.
- **Write-path fuzzing is out of scope** — `write()` consumes trusted,
  already-decoded `SipiImage` state, not attacker-controlled bytes; different
  threat model.
- **Sanitizer verification is Linux-CI-only** (macOS local ASan link is broken);
  same constraint as the existing parser harness.

#### Research insights — harness specifics (Phase 1)

- **Input delivery [adopt]:** one fixed path per process, truncate+rewrite,
  honoring `TEST_TMPDIR` with a `std::filesystem::temp_directory_path()`
  fallback — reuse the `sipi::test::tmp_dir()` shape (`test/test_paths.h:55-63`).
  mkstemp-per-iteration and memfd/fmemopen rejected (single-threaded process;
  no in-memory API exists).
- **libFuzzer flags:** `-timeout=25` (matches the parser target's precedent in
  `fuzz.yml`); `-rss_limit_mb` at default 2048 for the plain loop and `0` under
  the ASan pass (let ASan drive OOM detection); `-malloc_limit_mb` left at 0
  (inherits rss limit — this is what catches a single crafted-header huge
  allocation before `validate_decode_dims` fires); explicit small `-max_len`
  per target (~8–16 KB TIFF/JPEG/PNG, ~32 KB J2K) — the DEV-6418 bug class is
  header/marker parsing, and small caps concentrate mutation budget there.
- **Dictionaries [adopt]:** vendor AFL++'s `tiff/jpeg/png.dict` as
  `fuzz/dicts/` and wire via `cc_fuzz_test`'s `dicts` attribute (one-line BUILD
  change). No canonical JP2 dict exists — defer authoring one until coverage
  data demands it.
- **Leak detection:** PNG/JPEG's longjmp error paths are a documented LSan
  false-positive source (destructors skipped past the jump). Run one scoped
  experiment with `detect_leaks=1`; expected outcome is `ASAN_OPTIONS=detect_leaks=0`
  for the setjmp-based codec targets only (TIFF/J2K unwind via real exceptions
  and can keep it on). Never disable repo-wide.
- **Corpus:** strictly per-format corpus dirs and per-target `fuzz-corpus-<format>`
  artifacts; seed with the smallest valid fixtures (e.g. `mario.tif`/`mario.png`,
  not pyramid/CMYK variants) + the crafted `malformed/` files; the existing
  `-merge=1` nightly minimization fans out unchanged. Do NOT hand-author a
  malformed PNG fixture for symmetry — mutation from valid seeds is correct.
- **Structure-aware JP2 fuzzing: skip** for this pass; revisit only on a
  measured coverage plateau in the nightly stats (the bug class lives in
  front-of-file header parsing that byte-level mutation reaches).

### Layout cleanup (Phase 2)

- **Convention.** Every package folder under `src/` organizes its sources into
  `cpp/` and/or `rust/` subfolders; no folder carries a `-rs` suffix. The
  precedent already exists in-repo: `src/iiifparser/{cpp,rust,fuzz,corpus}/`,
  `src/scripting/rust/`, `src/throttling/cpp/`. Phase 2 generalizes it:
  `server-rs` → `server/rust`, `cli-rs` merges into `cli/rust` beside
  `cli/cpp`, single-language packages gain a `cpp/` subfolder. `fuzz/` and
  `corpus/` stay sibling subfolders (iiifparser precedent), so the Phase 1
  harness at `src/formats/fuzz/` rides the package rename to
  `src/format_handlers/fuzz/` unchanged.
- **`formats` → `format_handlers` rename.** The package takes the active
  glossary term (*Format handler* — the classes that implement `SipiIO`) over
  the passive category name, completing Probe 3 of the 2026-05-08 analysis.
  Whole-folder rename: sources land in `format_handlers/cpp/`, `fuzz/` and
  future `corpus/` ride along, labels/scope vocabulary/ARCH-MAP follow.
- **Hub dissolution — full ADR-0007 structural shape.** The `//src:engine`
  target and the loose `src/`-root files dissolve into the final packages the
  2026-05-08 modularization analysis (Probes 1 and 4) and ARCH-MAP already
  name. No `src/engine/` folder is created; "engine" remains ARCH-MAP umbrella
  vocabulary for the whole C++ side. Disposition:

  | Source (today) | Destination |
  |---|---|
  | `SipiImage.{h,cpp}`, `SipiImageError.h`, `populate_from_image.*`, `SipiIO.h` | `src/image/cpp/` (`//src/image`) |
  | ~12 processing methods (crop/scale/rotate/colour/channel/bit-depth/watermark/arithmetic) | extracted as behavior-preserving free functions in `src/image_processing/cpp/` (`//src/image_processing`) |
  | `resample.{cc,h}`, `process_benchmark.cpp` | `src/image_processing/cpp/` (free functions + the benchmark of exactly these ops, colocated per ADR-0003) |
  | `SipiCache.{h,cpp}` | `src/cache/cpp/` (`//src/cache`) |
  | `SipiCommon.*`, `SipiFilenameHash.*` | `src/util/cpp/` |
  | `SipiConf.{h,cpp}` | `src/ffi/cpp/` — NOT cli: `src/ffi/init.cpp` (production `sipi_init`) constructs it, and `cli → ffi` is the documented one-way direction (`src/ffi/BUILD.bazel:39-42`); ARCH-MAP's "CLI's config object" line is stale |
  | `SipiReport.cpp` | `src/cli/cpp/` (genuinely CLI-only) |
  | `SipiError.{h,cpp}` | `src/error/cpp/` (`//src/error`) — its own foundational package, the `//src:sipi_top` role made explicit; `metadata`/`format_handlers`/`iiifparser` depend on it *without* depending on `image` (folding it into `image` would close the unrepresentable `image → metadata → image` Bazel cycle). Phase 4 reshapes the error types in this package |

  `format_registry.cpp` stays in `format_handlers/cpp/` and keeps sole
  ownership of `SipiImage::io`; the link-time inversion (image declares,
  format_handlers defines, no Bazel edge image → format_handlers) is carried
  forward unchanged, and the same declare-here/link-resolve-there pattern is
  kept for `Sipi::read_watermark` (declared where `image_processing`'s
  watermark function needs it, defined in `format_handlers`, no BUILD edge).

  The method extraction is a *behavioral-shape* change but preserves behavior
  exactly — approval goldens stay byte-identical, and the hot-path benchmark
  gate applies. Mutating operations (crop, scale, rotate, bit-depth, channel
  ops, watermark) become free functions over `Image&`; only observers
  (compare, `operator+`/`operator-`) take `const Image&`. To give
  `image_processing` access without new friendships, the public mutator
  surface ADR-0007 names (`pixels_writable()` + metadata setters) is added
  **in Phase 2**; the four format-handler `friend` declarations stay until
  Phase 6. Note the seam files (`serve_image.cpp`, `image_handle.cpp`, CLI
  commands) are touched three times on the branch — Phase 2 (call-site
  rewrite), Phase 6 (`Result` returns), Phase 8 (seam consumes `Result`) —
  and goldens/benchmarks are re-verified at each of those phases, not once.
  This front-loads ADR-0007's structure so Phase 6 only *retypes* (throw →
  `Result`) and never moves code again.
- **Commit hygiene.** Pure whole-file moves and extraction rewrites land as
  separate commits (moves stay rebase-mechanical); the phase sits before the
  migration in the commit order so every migration commit is written against
  final paths, and after the fuzzer so the harness exists (as a branch-local
  regression net) throughout the refactor work.

#### Research insights — topology (Phase 2, dune review)

- **`read_watermark` directionality (critical):** "no BUILD edge" is only safe
  stated precisely. Today the compile-time signature check works because
  `formats → engine` already exists; post-split, `//src/format_handlers` must
  gain the mirrored explicit dep on the watermark-declaring header in
  `//src/image_processing` (the reverse edge would cycle). Alternative worth
  weighing at implementation: hoist the shared declaration into a small
  dependency-free leaf header (the `output_sink` pattern) instead of a third
  instance of the asymmetric link trick.
- **Boundary rules need `bazel query` acceptance checks** (repo precedent:
  iiifparser's FFI-freedom check in ARCH-MAP): `deps(//src/image_processing/...)`
  returns only {image, error, util, logging, observability};
  `somepath(//src/image, //src/image_processing)` is empty (no facade methods
  on `Image` — the FFI/Lua binding layer is the sole translator, and a facade
  would cycle); `deps(//src/error/...)` matches its minimal documented set.
- **Local-context kits stay ≤7 files:** consolidate `image_processing` under
  one or two umbrella headers rather than 12 per-operation header pairs, or
  the ARCH-MAP kit budget breaks.
- **`strip_include_prefix` twinning:** name the pattern explicitly in
  CONVENTIONS (`strip_include_prefix = "/src/<pkg>/cpp"` +
  `include_prefix = "<pkg>"`, iiifparser precedent) so virtual include paths
  (`#include "metadata/foo.h"`) survive the `cpp/` physical depth.
- **Glossary:** `UBIQUITOUS_LANGUAGE.md` gains the **Image processing** entry
  (ADR-0007's pending glossary delta).
- **Verification technique (learning):** a pre-Phase-2 worktree oracle (old
  layout vs new, same inputs) is the cheap way to prove byte-identical
  approval outputs across the moves
  (dasch-specs `learnings/best-practices/visual-parity-oracle-worktree-ab-for-big-bang-ui-refactors.md`).
  When touching CI, remember same-repo composite actions resolve `@main`, not
  the PR branch (`learnings/configuration-errors/github-actions-composite-action-main-ref-pr-isolation.md`),
  and test-fixture moves may want `copy_to_directory` over `filegroup` where
  `realpath` consistency matters
  (`learnings/best-practices/sipi-nix-to-bazel-migration-lessons.md` item 15).

### Error-migration landscape (Phases 3–8)

- **Name collision.** `Sipi::SipiError` already exists
  (`src/SipiError.h:34-65`, `: public shttps::Error`, with `SipiSizeError`
  deriving from it) and is actively caught for HTTP-400 dispatch. The "canonical
  `SipiError` value type" from DEV-7058 cannot take that name without folding
  the existing hierarchy. The ADR must enumerate **all** current mechanisms —
  `SipiImageError` (+ `SipiImageClientAbortError`), `SipiError`/`SipiSizeError`,
  and the bare `InfoError` enum (caught in `src/ffi/image_handle.cpp:170`) —
  and state what the new type unifies, replaces, or leaves alone.
- **Policy mapping is richer than client-safe/diagnostic.** The seams dispatch
  on exception *type*: `SipiSizeError`→400, `SipiImageError`→500 + Sentry report
  with `ImageContext`, `std::bad_alloc`→500 + `memory_alloc_failures_total`
  metric (no Sentry), `SipiImageClientAbortError`→ skip Sentry entirely. The
  value type needs a discriminant reproducing all of this, not a two-way split.
- **`std::bad_alloc` stays exception-based permanently** (resource exhaustion,
  not malformed input); the seam try/catch never fully disappears.
- **The `SipiIO` vtable constrains per-handler migration.** `read`/`read_shape`/
  `write` are pure-virtual with fixed signatures; one override cannot change its
  return type alone. "Boundary-complete per handler" therefore means: the
  handler's *internals* become Result-shaped, re-wrapped to bool/throw at the
  override boundary; the vtable (base + all four overrides + the `SipiImage`
  dispatcher) flips in **one dedicated step** after the last handler's internals
  are pure. The ADR records this pattern.
- **Per-codec error-path mechanics differ:**
  - **J2K**: Kakadu already throws real C++ exceptions (`kdu_exception`, 9 catch
    sites) — no setjmp boundary to redesign; structurally the easiest first.
  - **PNG / JPEG**: setjmp/longjmp bridges (3 sites each, RAII declared before
    `setjmp`); re-target the post-longjmp frame to return an error.
  - **TIFF**: `tiffError`/`tiffWarning` are currently **commented-out no-ops**
    (`SipiIOTiff.cpp:428-444`) — libtiff errors are silently swallowed. They
    were silenced because malformed UTF-8 in libtiff's format strings caused
    segfaults; the TIFF phase must solve that formatting-safety problem, not
    just re-enable the callbacks. Hardest; goes last.
  - Migration order: **J2K → PNG → JPEG → TIFF** (easiest-first; reverses the
    sub-issue numbering deliberately).
- **Three seams, not one.** (1) `src/ffi/serve_image.cpp` (HTTP; already
  `std::expected<_, SipiStatus>`-shaped internally — cleanest first mover),
  (2) `src/ffi/image_handle.cpp` (Lua userdata surface behind
  `src/scripting/rust/bindings/image.rs`; ~10 try/catch sites returning raw
  `int` status), (3) the CLI offline verbs (`src/cli/cli_app.cpp:127,287-435`,
  `src/cli/commands/verify.cpp:94-97`, `convert_access_file.cpp:85-259`). All
  three belong to Phase 8. ADR-0007's `SipiLua.cpp` reference is stale — that
  file no longer exists.
- **ADR-0007 is partly stale** and is refreshed as Phase 2's first deliverable,
  before its structural shape is executed: `pixels` is already
  `std::vector<byte>` (`SipiImage.h:110`), only 4 friend declarations remain
  (no `Icc` friend), and the `SipiLua.cpp` reference must become
  `image_handle.cpp`/`image.rs`. Its structure (`image/` + `image_processing/`)
  lands in Phase 2; its API consequences (friend removal, `app14_transform`)
  land in Phase 6 with the `Result` retyping.
- **Hot-path rule applies per handler.** `SipiIO::read`/`write` are the decode/
  encode hot path; exception→`expected` control-flow changes can shift codegen.
  Each handler phase requires a before/after `decode_benchmark` /
  `encode_benchmark` run per CLAUDE.md's benchmark gate.
- **Test churn is real.** 14+ files under `test/unit/` assert on throw behavior
  (`EXPECT_THROW` etc.); each handler phase updates its share explicitly.
- **Dangling doc reference.** `SipiImageError.h` points at
  `docs/src/development/error-model.md`, which does not exist; the ADR phase
  creates it or fixes the docstring, and updates `SipiImageError.h`'s
  subclassing-pattern guidance once type-ordered catches stop being the policy
  dispatch mechanism.

#### Research insights — error migration (Phases 3–8)

External precedent validates the plan's three load-bearing decisions: a single
canonical error type (the LLVM `Error`/Abseil `Status` shape, not per-function
`E` types), boundary-complete-per-module staging, and the intermediate-only
throwing adapter. Additions:

- **Illustrative error-type sketch** (final shape is the Phase 3 ADR's): an
  `ErrorCode` enum + a `constexpr ErrorPolicy policy_for(ErrorCode)` table
  carrying `{HttpStatusClass, SentryPolicy, metric_hint}` — the policy
  discriminant becomes *data*, replacing today's catch-by-type dispatch with
  one lookup. Two message accessors preserve the existing split:
  `client_message()` (path-redacted, no source location, mirrors
  `SipiImageError::message()`) and `diagnostic_message()` (mirrors
  `to_string()`). Keep `E` cheap to move: code + `std::string` diagnostic +
  `std::source_location`; no eager formatting on construction beyond the
  message the throw site already builds today. `std::bad_alloc` is deliberately
  NOT an `ErrorCode`.
- **setjmp bridge (JPEG, and PNG if landing-site style is kept):**
  `return std::unexpected(...)` from the `if (setjmp(...))` landing block is
  exactly as safe as today's `throw` from that block — the RAII-before-`setjmp`
  discipline is about destructors across `longjmp`, orthogonal to throw-vs-
  return. Manual cleanup (`free(icc_buffer_guard)`, `jpeg_destroy_decompress`)
  stays in the landing block; `jerr.error_message` stays a `char[]` (it must
  survive the jump — don't "modernize" it to `std::string`). Watch the classic
  volatile-locals footgun: any local modified between `setjmp` and `longjmp`
  and read after landing must be `volatile` (or hoisted).
- **PNG alternative for the ADR:** libpng officially supports replacing
  longjmp with a C++ throw from the error callback (`png_set_longjmp_fn` /
  custom error fn) — caught at the handler boundary and converted to `Result`.
  Potentially simpler than landing-site translation; libjpeg has NO such
  sanctioned escape hatch (raw C struct callbacks), so JPEG keeps the
  landing-site pattern regardless. Decide per-codec in the ADR.
- **Style rule for the ADR:** early-return (`if (!r) return std::unexpected(...)`)
  inside the C-interop handler internals; monadic `and_then`/`transform`
  composition only at seams where steps are already clean `Result`-returning
  calls (Image factories, `image_processing`). `[[nodiscard]]` on every
  `Result`-returning function; clang-tidy's `bugprone-unused-return-value`
  covers `std::expected` in its default type list (catches `(void)`-casts that
  `[[nodiscard]]` misses) — cite it in the Phase 8 CONVENTIONS rule.
- **Performance:** the happy-path cost story is contested in the literature
  (exceptions are "zero-cost" until thrown; `expected` adds a tag check that
  may not inline through virtual boundaries). The phase most at risk is **5e
  (vtable flip)** — the virtual-call ABI boundary is where inlining dies — so
  5e gets its own benchmark run, separate from 5a–5d (whose changes are
  error-path-dominated and well-documented as a win: measured 10–20× faster
  error paths for value returns vs throw).
- **Error-variant principle (learning):** enumerate failure variants per
  operation; never a global catch-all that silently genericizes a new failure
  (dasch-specs `learnings/best-practices/shared-tapir-error-envelope-leaks-exception-messages.md`,
  same principle in Scala).

### Sequencing against DEV-6418 wave-1

The codec memory-safety remediation
(`docs/specs/2026-08-27-sipi-codec-memory-safety/`) is in flight on this same
branch and touches the same files. **Wave-1 lands and merges first.** Fuzzing
before it merges would rediscover known bugs as "new" findings; migrating error
paths under it would create conflicts on `SipiImageError` construction sites.
Phase 1 starts from a clean post-wave-1 baseline (which the memory-safety plan
itself calls its precondition for the harness).

## Implementation Phases

#### Phase 0: Preconditions

- [ ] DEV-6418 wave-1 (codec memory-safety remediation) merged to `main`
- [ ] `git lfs pull` verified in the working tree (malformed fixtures are real bytes, not pointers)

#### Phase 1: Codec fuzz harness (DEV-7066)

- [ ] `src/formats/fuzz/` package with a shared harness helper (tmpfile write, `SipiImageError`/`std::exception` catch, `read_shape` + `read` invocation)
- [ ] `cc_fuzz_test` target `tiff_decode_fuzz` with TIFF seed corpus
- [ ] `cc_fuzz_test` target `jpeg_decode_fuzz` with JPEG seed corpus
- [ ] `cc_fuzz_test` target `png_decode_fuzz` with PNG seed corpus
- [ ] `cc_fuzz_test` target `j2k_decode_fuzz` with JP2 seed corpus
- [ ] Seed corpora as filegroups over `test/_test_data/images/malformed/` + the smallest valid fixtures per format from `test/_test_data/images/unit/` and `iso-15444-4/` (small seeds, strictly per-format dirs)
- [ ] AFL++ `tiff`/`jpeg`/`png` dictionaries vendored under `fuzz/dicts/` and wired via `cc_fuzz_test` `dicts`; libFuzzer flags set per Research Insights (`-timeout=25`, per-format `-max_len`, ASan pass with `-rss_limit_mb=0`)
- [ ] `detect_leaks` decision made from one scoped experiment (expected: `detect_leaks=0` for the setjmp-based PNG/JPEG targets only)
- [ ] Corpus-replay mode verified riding in the `//src/...` sweeps (`just bazel-test`) on all three platforms
- [ ] `fuzz.yml`: codec targets added to the nightly mutation loop (build+fuzz, minimize, ASan-paired pass), iiifparser target retained
- [ ] `fuzz.yml`: `GH_TOKEN: ${{ secrets.DASCHBOT_PAT }}` added to the Bazel steps (Kakadu edge now present); stale "no DASCHBOT_PAT" comment removed
- [ ] `just fuzz` / `fuzz-corpus-merge` recipes extended to the codec targets
- [ ] RF2 (J2K dimension-overflow) input reproduced by the harness and pinned in the seed corpus
- [ ] `parse_photoshop`-overshoot corpus entry confirmed present (from the malformed fixture)
- [ ] Crash-triage workflow documented in `fuzzing.md`: each finding gets a separate `fix:` commit on this branch (the bug exists on `main`, so `fix:` is the correct type), a corpus-pinned reproducer, and a Linear issue
- [ ] `docs/src/development/fuzzing.md` updated (targets, corpus dirs, credential note)

#### Phase 2: Layout cleanup — hub dissolution (ADR-0007 shape) + language subfolders (DEV-7067)

- [ ] ADR-0007 refreshed/reconciled first (vector-pixels already landed, 4 friends not 5, `SipiLua.cpp` → `image_handle.cpp`/`image.rs`, status → accepted) — the structural authority Phase 2 executes
- [ ] Raw-pixel-use audit doc (ADR-0007's gating deliverable, scoped to what remains after the vector swap) — gates the extraction
- [ ] Convention recorded in `CONVENTIONS.md` (module layout): every package folder under `src/` organizes sources into `cpp/` and/or `rust/` subfolders; no `-rs`-suffixed folders; the `strip_include_prefix = "/src/<pkg>/cpp"` + `include_prefix = "<pkg>"` twinning pattern named explicitly (iiifparser precedent) so virtual include paths survive the `cpp/` depth
- [ ] `//src/error` package created: `SipiError.*` → `src/error/cpp/` (the `sipi_top` foundational role; `metadata`/`format_handlers`/`iiifparser`/`image` all depend on it — never the reverse)
- [ ] `//src/image` package created: `SipiImage.*`, `SipiImageError.h`, `populate_from_image.*`, `SipiIO.h` → `src/image/cpp/`
- [ ] Public mutator surface added to `SipiImage` (`pixels_writable()` + metadata setters, ADR-0007) so `image_processing` needs no friendship; the four format-handler `friend` declarations stay until Phase 6
- [ ] `//src/image_processing` package created: the ~12 processing methods extracted as behavior-preserving free functions (`Sipi::processing::crop/scale/rotate/...` — mutators over `Image&`, observers over `const Image&`); `resample.{cc,h}` and `process_benchmark.cpp` moved in
- [ ] `Sipi::read_watermark` boundary pinned down per the dune review: EITHER `format_handlers` gains the explicit dep on the declaring `image_processing` header (mirroring today's `formats → engine` edge; the reverse edge would cycle) OR the declaration is hoisted into a dependency-free leaf header (`output_sink` pattern) — never "no edge either direction"
- [ ] `bazel query` acceptance checks recorded in ARCH-MAP for the new boundaries: `image_processing` deps only {image, error, util, logging, observability}; `somepath(image, image_processing)` empty (no facade methods — the FFI/Lua binding layer is the sole translator); `error` deps match its documented minimal set
- [ ] All extraction call sites rewritten: `src/ffi/serve_image.cpp`, `src/ffi/image_handle.cpp`, CLI commands, the direct-call test files (`test/unit/sipiimage/*` incl. `scale_resample_test`, `pixel_accessor_regression_test`, `overflow_regression_test`, `iiif_transform_matrix_test`, `channel_count_regression_test`, and `test/approval/image_encode_baseline_test.cpp`); Lua-visible `SipiImage` API unchanged (the binding layer absorbs the rewrite)
- [ ] `test/unit/sipiimage/` split and relocated per ADR-0003 colocation: format-handler regression tests (`tiff_bilevel`, `jpeg_write`, `jpeg_marker`, `jpeg_format`, `j2k_palette`, …) → `format_handlers`; image/processing tests → their new packages
- [ ] `//src/cache` package created: `SipiCache.*` → `src/cache/cpp/`
- [ ] `src/formats/` renamed to `src/format_handlers/` (active glossary term *Format handler* over the passive category; completes Probe 3 of the 2026-05-08 analysis): `//src/format_handlers:format_handlers` + `:output_sink`, the Phase 1 fuzz harness and corpora ride along as `src/format_handlers/fuzz/`, `tools/formats-fanout.sh` renamed/updated, commit-scope vocabulary updated (`formats` → `format_handlers`)
- [ ] Strays rehomed: `SipiCommon.*` + `SipiFilenameHash.*` → `src/util/cpp/`; `SipiConf.*` → `src/ffi/cpp/` (its production consumer is `init.cpp`; `cli → ffi` direction preserved); `SipiReport.cpp` → `src/cli/cpp/`
- [ ] `//src:engine` / `:sipi_lib` / `:sipi_top` root targets dissolved or reduced accordingly; no loose sources remain at the `src/` root
- [ ] Before/after benchmark run recorded for the extraction (decode + transform paths; hot-path rule)
- [ ] `src/server-rs/` → `src/server/rust/`
- [ ] `src/cli/` + `src/cli-rs/` → `src/cli/cpp/` + `src/cli/rust/`
- [ ] Single-language packages (`format_handlers` (post-rename), `metadata`, `ffi`, `util`, `logging`, `observability`) gain `cpp/` subfolders; `fuzz/` and `corpus/` remain sibling subfolders (iiifparser precedent)
- [ ] Already-conforming packages verified unchanged (`iiifparser/{cpp,rust}`, `scripting/rust`, `throttling/cpp`)
- [ ] All Bazel labels, `strip_include_prefix`/`includes`, and `#include` paths updated
- [ ] `justfile` recipes, `.bazelrc` (e.g. `--instrumentation_filter`), and CI workflows (`ci.yml`, `fuzz.yml`, `publish.yml`) updated to the new labels
- [ ] Docs scrubbed repo-wide: CLAUDE.md component table, CONVENTIONS.md module-layout table (new `error`/`image`/`image_processing`/`cache` rows, `formats` → `format_handlers`) + commit-scope vocabulary, testing/fuzzing docs, `UBIQUITOUS_LANGUAGE.md` **Image processing** entry (ADR-0007's pending glossary delta) — no stale `-rs` or old-path references anywhere
- [ ] Load-bearing code-comment banners updated (not just docs): `format_registry.cpp` file header, the `read_watermark` doc comments in `SipiImage.h`/`SipiIOTiff.h`, the `//src` BUILD docstrings
- [ ] ARCH-MAP.md rewritten for the new topology (new `image`/`image_processing`/`error` component entries, `cache` paths, `format_handlers` rename) — a component rewrite, not a path scrub
- [x] Phase 2 tracked as DEV-7067 (sub-issue of DEV-7056)
- [ ] Behavior-preservation verification: `just bazel-build` + full test sweeps green on macOS; approval goldens byte-identical (moves AND extraction)

#### Phase 3: Error-strategy ADR (DEV-7057)

- [ ] ADR `docs/adr/00NN-value-based-image-errors.md` written and reviewed
- [ ] Decision: name + shape of the canonical error value type, reconciling the existing `SipiError`/`SipiSizeError`, `SipiImageError`/`SipiImageClientAbortError`, and `InfoError` mechanisms
- [ ] Decision: policy-mapping discriminant (HTTP status, Sentry yes/no + `ImageContext`, metric increments, client-abort skip) reproduced in the value type
- [ ] Decision: `Result<T>` alias definition and header location
- [ ] Decision: `SipiIO` vtable strategy recorded (internals-first per handler, wrap at override boundary, single vtable-flip step)
- [ ] Decision: handler migration order J2K → PNG → JPEG → TIFF with rationale
- [ ] Decision: per-codec longjmp bridge mechanism — PNG may use libpng's sanctioned throw-from-error-callback (`png_set_longjmp_fn`) converted to `Result` at the handler boundary; JPEG keeps the setjmp landing-site pattern (libjpeg has no sanctioned escape hatch)
- [ ] Decision: `std::bad_alloc` and logic-bug invariants stay exception-based
- [ ] Decision: the temporary `Result`→throw facade adapter (introduced Phase 5e, retired Phase 6) recorded as intermediate-commit-only state — it never ships, since all phases merge in one PR, so the no-backwards-compat-shims rule is not violated in shipped code
- [ ] `docs/src/development/error-model.md` created (or the dangling reference in `SipiImageError.h` fixed)

#### Phase 4: Foundation (DEV-7058)

- [ ] Canonical error value type implemented per the ADR (code + client-safe message + diagnostic, path-redaction preserved)
- [ ] `Result<T>` alias introduced
- [ ] Seam conversion helpers (value → `SipiStatus`, value → report struct) implemented without migrating any caller
- [ ] Unit tests for the new type and helpers

#### Phase 5a: SipiIOJ2k internals (DEV-7063)

- [ ] `SipiIOJ2k.cpp` internals converted to `Result` (read, read_shape, write together); `kdu_exception` caught at the Kakadu edge only
- [ ] Override boundary re-wraps `Result` → bool/throw (vtable unchanged)
- [ ] Affected unit tests updated from throw-assertions to Result/behavior assertions
- [ ] Before/after `decode_benchmark`/`encode_benchmark` run recorded (U-test + CV rule)
- [ ] Fuzz harness J2K catch-scope note updated if reject-path behavior changed

#### Phase 5b: SipiIOPng internals (DEV-7062)

- [ ] `SipiIOPng.cpp` internals converted to `Result` via the ADR-decided bridge (landing-site returns, or libpng's throw-from-callback converted at the handler boundary)
- [ ] Override boundary re-wraps `Result` → bool/throw
- [ ] Affected unit tests updated
- [ ] Before/after benchmark run recorded

#### Phase 5c: SipiIOJpeg internals (DEV-7061)

- [ ] `SipiIOJpeg.cpp` internals converted to `Result`; `setjmp` landing sites return errors instead of throwing
- [ ] `parse_photoshop`/metadata try/catch (`SipiIOJpeg.cpp:670-671`) left in place with a deferral note — it wraps the `Iptc`/`Exif`/`Xmp` constructors that become `Result` factories only in Phase 7, which deletes it
- [ ] Override boundary re-wraps `Result` → bool/throw
- [ ] Affected unit tests updated
- [ ] Before/after benchmark run recorded

#### Phase 5d: SipiIOTiff internals (DEV-7060)

- [ ] `tiffError`/`tiffWarning` handlers re-enabled with format-string/UTF-8 safety (root-cause the historical segfault, don't just uncomment)
- [ ] `SipiIOTiff.cpp` internals converted to `Result`; libtiff error handler bridges to error-return
- [ ] Override boundary re-wraps `Result` → bool/throw
- [ ] Affected unit tests updated
- [ ] Before/after benchmark run recorded

#### Phase 5e: vtable flip

- [ ] `SipiIO` base signatures changed to `Result`-returning; all four overrides drop their wrap layer in the same change
- [ ] `SipiImage` dispatcher (`read`/`read_shape`/`write` facade) consumes `Result`, converting to throw at its own boundary temporarily (retired in Phase 6, within the same PR)
- [ ] Full test + approval suite green (goldens byte-identical — behavior preservation)
- [ ] Dedicated before/after benchmark run for the flip itself — the virtual-call ABI boundary is where a happy-path codegen regression would appear, separately from the 5a–5d error-path changes

#### Phase 6: Hub — Image + image_processing (DEV-7059)

- [ ] `Image` construction → `Result`-returning factories per the ADR
- [ ] The `image_processing` free functions (extracted in Phase 2) converted to `Result` returns; call sites updated
- [ ] Friend declarations removed: format handlers switch to the Phase 2 public mutator surface (`pixels_writable()` + metadata setters)
- [ ] `app14_transform` field removed: JPEG handler inverts CMYK/YCCK at decode; downstream sees standard CMYK (ADR-0007)
- [ ] Temporary `Result`→throw adapter at the facade retired
- [ ] Affected unit tests updated; approval goldens verified unchanged
- [ ] Before/after benchmark run recorded (transform paths)

#### Phase 7: Metadata parsers (DEV-7064)

- [ ] `Exif` construction → `Result` factory
- [ ] `Iptc` construction → `Result` factory
- [ ] `Xmp` construction → `Result` factory
- [ ] `Icc` construction → `Result` factory (ICC determinism invariant: all emission still funnels through `iccBytes()`)
- [ ] Handler call sites updated; the Phase 5c try/catch fully deleted
- [ ] Affected unit tests updated
- [ ] Before/after `decode_benchmark` run recorded (metadata construction runs inside `read()` — hot-path rule applies)

#### Phase 8: Seams + retirement (DEV-7065)

- [ ] `src/ffi/serve_image.cpp` consumes `Result` directly; internal try/catch reduced to `std::bad_alloc` + invariant catches only
- [ ] `src/ffi/image_handle.cpp` (Lua surface) consumes `Result` directly; Lua-visible error strings verified unchanged (or changes documented)
- [ ] CLI verbs (`cli_app.cpp`, `commands/verify.cpp`, `commands/convert_*.cpp`) consume `Result`; exit codes and stderr output verified unchanged
- [ ] `SipiImageError` reduced to the truly-unrecoverable role (or removed per the ADR); subclassing-pattern docstring updated
- [ ] Remaining throwing convenience wrappers removed
- [ ] Fuzz harness catch blocks narrowed to the post-migration contract
- [ ] CONVENTIONS.md rule (or reviewer-guidelines entry) added: new fallible ops in the image layer return `Result`, `[[nodiscard]]` mandatory; cite clang-tidy `bugprone-unused-return-value` (covers `std::expected` by default, catches `(void)`-casts that `[[nodiscard]]` misses)
- [ ] `UBIQUITOUS_LANGUAGE.md` / docs updated where error-model terms changed

## Acceptance Criteria

- [ ] Codec fuzz targets for TIFF/JPEG/PNG/J2K exist, corpus-seeded, running nightly and as corpus-replay in the `//src/...` sweeps (DEV-7066 "done when")
- [ ] The `parse_photoshop`-overshoot and J2K dimension-overflow inputs are in the corpus and replay clean
- [ ] After Phase 8: no `throw SipiImageError` remains on a fallible-op path in `src/format_handlers/`, `src/image/` + `src/image_processing/`, `src/metadata/`; exceptions remain only for OOM/invariants
- [ ] Layout convention holds: no `-rs`-suffixed folders remain; every `src/` package organizes sources under `cpp/` and/or `rust/`; the hub is dissolved into `//src/image` + `//src/image_processing` + `//src/cache` + `//src/error` with no loose sources at the `src/` root and no `engine/` folder; `//src/format_handlers` replaces `//src/formats`
- [ ] Every phase lands with `just bazel-rustfmt-check` / `bazel-clippy-check` (where Rust is touched) and the full test suite green; approval goldens unchanged throughout
- [ ] Each handler migration justified by a before/after benchmark run per the hot-path rule
- [ ] HTTP status mapping, Sentry reporting, metrics, and CLI exit codes are behavior-identical before and after (verified per phase)

## Dependencies & Risks

- **DEV-6418 wave-1 must merge first** (same files; fuzzing a pre-fix baseline wastes triage; migration conflicts with in-flight fixes).
- **Kakadu credential gate**: nightly fuzz workflow now needs `DASCHBOT_PAT`; local `just fuzz` needs `gh auth` + org membership (already true for any build).
- **ADR-0007 refresh** is Phase 2's first deliverable and gates Phase 2's own structural execution; the error-strategy ADR (Phase 3) gates Phases 4–8. The handler phases depend on neither beyond the Phase 4 foundation types.
- **Bazel module-co-located layout**: `//src/formats` is already carved; Phase 2 dissolves the hub into `//src/image` + `//src/image_processing` + `//src/cache` (ADR-0007's structural shape) and generalizes the language-subfolder convention, fully satisfying the layout precondition the hub phase names.
- **Single-PR delivery**: all phases merge together (all-or-nothing); a long-lived branch must be kept rebased on `main` (release cadence continues underneath it), and the Phase 2 repo-wide moves make rebasing over unrelated `main` changes the branch's main friction point.

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Fuzzer finds new Criticals in throw-era code during branch work | M | M | That's the point — each lands as a separate `fix:` commit on this branch (bug exists on `main`, so `fix:` is the correct type; rebase-merge puts it on `main` verbatim); corpus pins each fix |
| Exception→`expected` codegen regression on decode hot path | L | H | Per-handler benchmark gate (Phase 5a–d checkboxes); fallback is reshaping the wrap layer, not reverting the migration |
| TIFF error-handler re-enable reintroduces the historical UTF-8 segfault | M | H | Root-cause first (Phase 5d explicit deliverable); fuzz harness is already on the branch and covers exactly this path |
| Behavior drift at the seams (HTTP status, Sentry, Lua error strings, CLI exit codes) | M | M | Policy-mapping discriminant decided in the ADR before any code; per-seam verification checkboxes in Phase 8 |
| Long-lived single-PR branch drifts from `main` (Phase 2 moves maximize rebase friction) | H | M | Keep the branch continuously rebased; land the Phase 2 moves as clean whole-file moves (no content edits in the same commit) so rebases stay mechanical |
| Phase 2 method extraction introduces a subtle behavior change at a rewritten call site | M | M | Extraction commits are behavior-preserving and separate from move commits; approval goldens byte-identical per commit; benchmark gate on the extraction; fuzz harness already on the branch |

## Success Metrics

- Nightly codec fuzzing runs green (or produces triaged findings) with corpus growth tracked via the two-tier policy.
- The DEV-6418 bug class (crafted-input memory corruption) is detected by machine, not by hand review.
- Failure contracts visible in signatures across the image layer; seam try/catch reduced to OOM + invariants.
- No approval-golden or benchmark regressions across the whole migration.

## References

- Linear: DEV-7066 (harness), DEV-7056 (umbrella) + sub-issues DEV-7057..7065, DEV-7067 (layout)
- Fuzz precedent: `src/iiifparser/fuzz/BUILD.bazel:32-46`, `src/iiifparser/fuzz/fuzz_target.cc:15-19`, `.bazelrc:318-389`, `.github/workflows/fuzz.yml`, `docs/src/development/fuzzing.md`, plan `docs/specs/2026-08-14-sipi-agent-legibility/02-feat-iiif-parser-fuzz-harness-plan.md`
- Entry points: `src/SipiIO.h:52-63,165-218`, `src/formats/format_registry.cpp:23-28`
- Error types & seams: `src/SipiError.h:34-65`, `src/SipiImageError.h:34-146`, `src/ffi/serve_image.cpp:56-77,446-805`, `src/ffi/serve_response.h:45-55`, `src/ffi/image_handle.cpp:104-690`, `src/scripting/rust/bindings/image.rs:65-118`
- Codec error mechanics: `src/formats/SipiIOJpeg.cpp:75-91,507-531,670-671`, `SipiIOTiff.cpp:428-482`, `SipiIOPng.cpp:119-125,366-496`, `SipiIOJ2k.cpp:191-239,698-782`
- Convention: `docs/src/development/cpp-style-guide.md:93-108,561-583,662-671,790`
- ADR-0007: `docs/adr/0007-sipiimage-decomposition.md` (proposed; partly stale — refreshed and executed in Phase 2)
- Modularization analysis (archived): `docs/archive/2026-05-08-modularization-analysis.md` (Probes 1, 3, 4 — the source of the target package names)
- Precursor: `docs/specs/2026-08-27-sipi-codec-memory-safety/01-fix-sipi-codec-memory-safety-plan.md` (follow-up charter at line 301) + journal
- Fixtures: `test/_test_data/images/malformed/README.md`, `test/_test_data/images/unit/` (Git LFS)
- Institutional learnings (dasch-specs `learnings/`): `build-errors/bazel-opt-mode-fortify-source-redefinition-werror.md` (already encoded at `.bazelrc:112-113` — keep the undef-then-redefine pattern for any new opt+sanitizer config), `best-practices/visual-parity-oracle-worktree-ab-for-big-bang-ui-refactors.md` (worktree oracle for Phase 2 byte-identity), `configuration-errors/github-actions-composite-action-main-ref-pr-isolation.md` (same-repo composite actions resolve `@main` — relevant to Phase 2 CI edits), `best-practices/sipi-nix-to-bazel-migration-lessons.md` (item 15 `copy_to_directory` fixtures; items on `--per_file_copt` are historical — the current fuzz config uses hermetic-toolchain flags), `best-practices/shared-tapir-error-envelope-leaks-exception-messages.md` (enumerate error variants per operation)
- External: LLVM `Error`/`Expected` and Abseil `Status`/`StatusOr` (canonical single-error-type precedent), P2505 monadic `expected`, libpng error-handling manual (`png_set_longjmp_fn`), LLVM libFuzzer docs (flag semantics), AFL++ `dictionaries/`, `rules_fuzzing` `cc_fuzz_test` `dicts` attribute
