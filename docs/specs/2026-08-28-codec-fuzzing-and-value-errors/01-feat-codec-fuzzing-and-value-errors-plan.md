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

- [x] DEV-6418 wave-1 (codec memory-safety remediation) merged to `main`
- [x] `git lfs pull` verified in the working tree (malformed fixtures are real bytes, not pointers)

#### Phase 1: Codec fuzz harness (DEV-7066)

- [x] `src/formats/fuzz/` package with a shared harness helper (tmpfile write, `SipiImageError`/`std::exception` catch, `read_shape` + `read` invocation)
- [x] `cc_fuzz_test` target `tiff_decode_fuzz` with TIFF seed corpus
- [x] `cc_fuzz_test` target `jpeg_decode_fuzz` with JPEG seed corpus
- [x] `cc_fuzz_test` target `png_decode_fuzz` with PNG seed corpus
- [x] `cc_fuzz_test` target `j2k_decode_fuzz` with JP2 seed corpus
- [x] Seed corpora as filegroups over `test/_test_data/images/malformed/` + the smallest valid fixtures per format from `test/_test_data/images/unit/` and `iso-15444-4/` (small seeds, strictly per-format dirs)
- [x] AFL++ `tiff`/`jpeg`/`png` dictionaries vendored under `fuzz/dicts/` and passed as explicit `-dict=` flags (NOT the `cc_fuzz_test` `dictionary` attribute — it reaches libFuzzer only through `rules_fuzzing`'s launcher, which this repo bypasses); libFuzzer flags set per Research Insights (`-timeout=25`, per-format `-max_len`, ASan pass with `-rss_limit_mb=0`)
- [x] `detect_leaks` decision recorded: `detect_leaks=0` on the ASan-paired pass for the setjmp-based PNG/JPEG targets only; TIFF/J2K keep leak detection on
- [ ] `detect_leaks` decision confirmed by the scoped experiment (Linux CI only — LSan is a Linux ASan feature and the local macOS ASan link is broken)
- [x] Corpus-replay mode verified riding in the `//src/...` sweeps (`just bazel-test`) on macOS
- [x] Corpus-replay mode verified on linux-x86_64 and linux-aarch64 (CI) — the branch's first full CI verdict landed on `7d0c7258` (run 33256167199) and every leg passed: `test / linux-amd64`, `test / linux-arm64`, `test / darwin-arm64`, plus `asan-ubsan / amd64`, `commit-lint` and `docs`. Ninety-odd commits deep, and the first Linux run of the whole migration is green
- [x] `fuzz.yml`: codec targets added to the nightly mutation loop (build+fuzz, minimize, ASan-paired pass), iiifparser target retained
- [x] `fuzz.yml`: `GH_TOKEN: ${{ secrets.DASCHBOT_PAT }}` added at job level (Kakadu edge now present, and the release fetch re-evaluates per Bazel invocation); stale "no DASCHBOT_PAT" comment removed; Git LFS switched on (codec seeds are LFS fixtures)
- [x] `just fuzz` / `fuzz-corpus-merge` recipes extended to the codec targets
- [x] RF2 (J2K dimension-overflow) input reproduced and pinned as `malformed/j2k_oversized_dimensions.jp2` — a J2K fuzz seed plus a unit-test assertion. Note: the reachable rejection is `validate_decode_dims` (the `kMaxDecodeDim` cap); with that cap in place the downstream `checked_buf_size` overflow branch is unreachable on 64-bit
- [x] `parse_photoshop`-overshoot corpus entry confirmed present (from the malformed fixture)
- [x] Crash-triage workflow documented in `fuzzing.md`: each finding gets a separate `fix:` commit on this branch (the bug exists on `main`, so `fix:` is the correct type), a corpus-pinned reproducer, and a Linear issue
- [x] `docs/src/development/fuzzing.md` updated (targets, corpus dirs, credential note); `CONVENTIONS.md` `formats` scope row updated for the `fuzz/`, `fuzz/dicts/` and `corpus/` subpackages

#### Phase 2: Layout cleanup — hub dissolution (ADR-0007 shape) + language subfolders (DEV-7067)

- [x] ADR-0007 refreshed/reconciled first (vector-pixels already landed, 4 friends not 5, `SipiLua.cpp` → `image_handle.cpp`/`image.rs`, status → accepted) — the structural authority Phase 2 executes. Also gained the full disposition table, the no-`engine/`-folder and standalone-`//src/error` reasoning, the `read_watermark` two-option boundary, and the `bazel query` invariants
- [x] Raw-pixel-use audit doc (ADR-0007's gating deliverable, scoped to what remains after the vector swap) — gates the extraction. Landed as `02-raw-pixel-use-audit-design.md`. Two findings need a decision before the extraction: lazy EXIF init needs a setter (not an accessor), and `app14_transform` removal is a genuine behavior change that must not ride inside the friend-removal change
- [x] Convention recorded in `CONVENTIONS.md` (module layout): every package folder under `src/` organizes sources into `cpp/` and/or `rust/` subfolders; no `-rs`-suffixed folders; the `strip_include_prefix = "/src/<pkg>/cpp"` + `include_prefix = "<pkg>"` twinning pattern named explicitly (iiifparser precedent) so virtual include paths survive the `cpp/` depth
- [x] `//src/error` package created: `SipiError.*` → `src/error/cpp/` (the `sipi_top` foundational role; `metadata`/`format_handlers`/`iiifparser`/`image` all depend on it — never the reverse)
- [x] `//src/image` package created: `SipiImage.*`, `SipiImageError.h`, `populate_from_image.*`, `SipiIO.h` → `src/image/cpp/`. `resample.{cc,h}` rode along (its only consumer is `SipiImage.cpp`) and moves on to `image_processing` with the extraction; `//src:engine` is dissolved
- [x] Public mutator surface added to `SipiImage` (`pixels_writable()` + metadata setters, ADR-0007) so `image_processing` needs no friendship; the four format-handler `friend` declarations stay until Phase 6
- [x] `//src/image_processing` package created: the ~12 processing methods extracted as behavior-preserving free functions (`Sipi::processing::crop/scale/rotate/...` — mutators over `Image&`, observers over `const Image&`); `resample.{cc,h}` and `process_benchmark.cpp` moved in
- [x] `Sipi::read_watermark` boundary pinned down per the dune review: EITHER `format_handlers` gains the explicit dep on the declaring `image_processing` header (mirroring today's `formats → engine` edge; the reverse edge would cycle) OR the declaration is hoisted into a dependency-free leaf header (`output_sink` pattern) — never "no edge either direction"
- [x] `bazel query` acceptance checks recorded in ARCH-MAP for the new boundaries: `image_processing` deps only {image, error, util, logging, observability}; `somepath(image, image_processing)` empty (no facade methods — the FFI/Lua binding layer is the sole translator); `error` deps match its documented minimal set
- [x] All extraction call sites rewritten: `src/ffi/serve_image.cpp`, `src/ffi/image_handle.cpp`, CLI commands, the direct-call test files (`test/unit/sipiimage/*` incl. `scale_resample_test`, `pixel_accessor_regression_test`, `overflow_regression_test`, `iiif_transform_matrix_test`, `channel_count_regression_test`, and `test/approval/image_encode_baseline_test.cpp`); Lua-visible `SipiImage` API unchanged (the binding layer absorbs the rewrite)
- [x] `test/unit/sipiimage/` split and relocated per ADR-0003 colocation: format-handler regression tests (`tiff_bilevel`, `jpeg_write`, `jpeg_marker`, `jpeg_format`, `j2k_palette`, …) → `format_handlers`; image/processing tests → their new packages
- [x] `//src/cache` package created: `SipiCache.*` → `src/cache/cpp/`
- [x] `src/formats/` renamed to `src/format_handlers/` (active glossary term *Format handler* over the passive category; completes Probe 3 of the 2026-05-08 analysis): `//src/format_handlers:format_handlers` + `:output_sink`, the Phase 1 fuzz harness and corpora ride along as `src/format_handlers/fuzz/`, `tools/formats-fanout.sh` renamed/updated, commit-scope vocabulary updated (`formats` → `format_handlers`)
- [x] Stray rehomed: `SipiFilenameHash.*` → `src/util/` (lands flat; it rides the `util` package into `cpp/` with the single-language-package sweep below). `SipiCommon.*` was **deleted instead of moved** — both files were empty translation units, so relocating them only moved dead weight
- [x] Strays rehomed: `SipiConf.{h,cpp}` → `src/ffi/` (its production consumer is `init.cpp`; `cli → ffi` direction preserved); `SipiReport.{h,cpp}` → `src/cli/` as its own `:sipi_report` leaf target. Both headers came out of the legacy `include/` tree, which left `//include:headers` with zero reverse dependencies. (Both land flat; they ride their packages into `cpp/` with the single-language-package sweep below)
- [x] `//src:engine` / `:sipi_lib` / `:sipi_top` root targets dissolved or reduced accordingly; no loose sources remain at the `src/` root
- [x] Before/after benchmark run recorded for the extraction (decode + transform paths; hot-path rule) — **both tiers now closed.** Process tier: `OVERALL_GEOMEAN +0.35 %` on a same-session A/B against a `865f2c25` worktree oracle. `BM_ScaleHigh/1024` reproduces at `+3.79 %` and `BM_Rotate45` at `−2.69 %` — a bidirectional, size-dependent shift in provably-equivalent code, i.e. a binary-layout effect; accepted per the maintainer's decision rule, with no alignment/`copt` hack added. Decode tier: `OVERALL_GEOMEAN −0.51 %` real / `+0.06 %` CPU, everything inside the tier's CV floor. See the journal's benchmark sections
- [x] `src/server-rs/` → `src/server/rust/`
- [x] `src/cli/` + `src/cli-rs/` → `src/cli/cpp/` + `src/cli/rust/`
- [x] Single-language packages (`format_handlers` (post-rename), `metadata`, `ffi`, `util`, `logging`, `observability`) gain `cpp/` subfolders; `fuzz/` and `corpus/` remain sibling subfolders (iiifparser precedent)
- [x] Already-conforming packages verified unchanged (`iiifparser/{cpp,rust}`, `scripting/rust`, `throttling/cpp`) — structure untouched; only 20 lines of label/path *references* inside them changed
- [x] All Bazel labels, `strip_include_prefix`/`includes`, and `#include` paths updated
- [x] `justfile` recipes, `.bazelrc` (e.g. `--instrumentation_filter`), and CI workflows (`ci.yml`, `fuzz.yml`, `publish.yml`) updated to the new labels — note the workflows needed no edits, they reference no moved label
- [x] Docs scrubbed repo-wide: CLAUDE.md component table, CONVENTIONS.md module-layout table (new `error`/`image`/`image_processing`/`cache` rows, `formats` → `format_handlers`) + commit-scope vocabulary, testing/fuzzing docs, `UBIQUITOUS_LANGUAGE.md` **Image processing** entry (ADR-0007's pending glossary delta) — no stale `-rs` or old-path references anywhere
- [x] Load-bearing code-comment banners updated (not just docs): `format_registry.cpp` file header, the `read_watermark` doc comments in `SipiImage.h`/`SipiIOTiff.h`, the `//src` BUILD docstrings
- [x] ARCH-MAP.md rewritten for the new topology (new `image`/`image_processing`/`error` component entries, `cache` paths, `format_handlers` rename) — a component rewrite, not a path scrub
- [x] Phase 2 tracked as DEV-7067 (sub-issue of DEV-7056)
- [x] Behavior-preservation verification: `just bazel-build` + full test sweeps green on macOS; approval goldens byte-identical (moves AND extraction) — `just bazel-test` run by the orchestrator after every chunk this round, green each time (70/70 through the extraction, 72/72 once the colocated tests joined the `//src/...` sweep). **Linux/CI verification is still outstanding for this round's commits** — the `//src:image` collision showed that a green macOS run cannot see a Linux-gated target

#### Phase 3: Error-strategy ADR (DEV-7057)

- [x] ADR `docs/adr/0024-value-based-image-errors.md` **written**
- [x] ADR **reviewed and accepted by the maintainer** — `status: accepted`. Its one contested point was settled against the ADR's original recommendation once the evidence surfaced: **PNG and JPEG both keep the setjmp landing-site pattern**, with the landing block returning `std::unexpected` instead of throwing. Both the acceptance and the reversal were folded into the introducing commit, so the ADR ships as `accepted` and states only the settled decision. Phases 4–8 unblocked
- [x] Decision: name + shape of the canonical error value type, reconciling the existing `SipiError`/`SipiSizeError`, `SipiImageError`/`SipiImageClientAbortError`, and `InfoError` mechanisms — `Sipi::SipiValueError` in `//src/error`; `SipiError`/`SipiSizeError` left alone, `SipiImageError`/`SipiImageClientAbortError`/`InfoError` replaced
- [x] Decision: policy-mapping discriminant (HTTP status, Sentry yes/no + `ImageContext`, metric increments, client-abort skip) reproduced in the value type — `ErrorCode` + a `constexpr policy_for()` table, policy as data
- [x] Decision: `Result<T>` alias definition and header location
- [x] Decision: `SipiIO` vtable strategy recorded (internals-first per handler, wrap at override boundary, single vtable-flip step)
- [x] Decision: handler migration order J2K → PNG → JPEG → TIFF with rationale
- [x] Decision: per-codec longjmp bridge mechanism — **PNG and JPEG are treated uniformly**: both keep `setjmp`/`longjmp` and change only the landing block's final control transfer from `throw` to `return std::unexpected`. Settled by the maintainer against the ADR's original PNG recommendation; the libpng throw-from-error-callback is recorded as the rejected option in the ADR's Considered Options, on three grounds (the shipped `fix:` `94f45a28` that removed throw-through-libpng as exception-through-C UB, libpng being a BCR build with no guaranteed unwind tables, and parity with JPEG's identical hazard)
- [x] Decision: `std::bad_alloc` and logic-bug invariants stay exception-based
- [x] Decision: the temporary `Result`→throw facade adapter recorded as intermediate-commit-only state — it never ships, since all phases merge in one PR, so the no-backwards-compat-shims rule is not violated in shipped code
- [x] `docs/src/development/error-model.md` created (or the dangling reference in `SipiImageError.h` fixed) — created; the existing docstring reference already pointed at exactly this path, so no header edit was needed

#### Phase 4: Foundation (DEV-7058)

- [x] Canonical error value type implemented per the ADR (code + client-safe message + diagnostic, path-redaction preserved) — `SipiValueError` + `ErrorCode` + `policy_for` in `//src/error`, header-only; `errnum`/`strerror` splice carried over from `SipiImageError` so migrated call sites emit byte-identical text
- [x] `Result<T>` alias introduced — same header, per ADR Decision 3
- [x] Unit tests for the value type (`//src/error:error_test`): `policy_for` totality against the `error-model.md` table, `constexpr` usability, both message shapes, the `errnum` splice, and `Result<T>` round-trips
- [x] Seam conversion helpers (value → `SipiStatus`, value → report struct) implemented without migrating any caller — `status_for` + `report_value_error` in `//src/ffi` (`serve_image.{h,cpp}`), where the seam's `SipiStatus` and `SipiImageErrorReport` live; `//src/error` became an explicit dep. Deliberately **two** helpers: the `MetricHint` side of the policy has no clean single conversion and belongs to Phase 8's seam work
- [x] Unit tests for the seam helpers (`//src/ffi:serve_image_test`), including the load-bearing `kSkip` case: a skipped policy must not invoke the report callback at all

#### Phase 5a: SipiIOJ2k internals (DEV-7063)

- [x] `SipiIOJ2k.cpp` internals converted to `Result` (read, read_shape, write together); `kdu_exception` caught at the Kakadu edge only — landed as four commits (`validate_j2k_palette_mapping` → `read_shape` → `read` → `write`); message text byte-identical at every site, `checked_buf_size`/`validate_decode_dims` untouched per the maintainer's ruling
- [x] Override boundary re-wraps `Result` → bool/throw (vtable unchanged) — `read`/`write`'s internals are **private static members** (friendship to `SipiImage`'s protected state is granted to the class, not the TU); `read_shape`'s is file-local since it needs none. `write`'s override keeps the `kClientAbort` → `SipiImageClientAbortError` branch, which the HTTP seam dispatches on to skip Sentry
- [x] Affected unit tests updated from throw-assertions to Result/behavior assertions — the six `validate_j2k_palette_mapping` direct-call assertions now check `error().code()` plus a distinguishing message substring; every test that drives `SipiImage::read`/`write` passes with its assertions **unedited**, which is the check that the external contract did not move
- [x] Before/after `decode_benchmark`/`encode_benchmark` run recorded (U-test + CV rule) — decode `OVERALL_GEOMEAN +0.01 %` real / `+0.42 %` CPU, all three JP2 cases inside the tier CV; encode `BM_EncodeJ2k` median `−1.45 %` real / `+0.50 %` CPU against its own 3.85 % CV. No regression on either tier; see the journal
- [x] Fuzz harness J2K catch-scope note updated if reject-path behavior changed — checked, **no edit needed**: the note explains why the bare `catch (...)` is there, and unguarded Kakadu calls (plus the three other codecs sharing the harness) can still raise a `kdu_exception`, so it remains accurate

#### Phase 5b: SipiIOPng internals (DEV-7062)

- [x] `SipiIOPng.cpp` internals converted to `Result`; the three `setjmp` landing sites return errors instead of throwing (`longjmp` stays, per the settled Decision 6) — landed as three commits (`read_shape` → `read` → `write`); message text byte-identical at every site; `sipi_error_fn` and `validate_decode_dims` untouched
- [x] Override boundary re-wraps `Result` → bool/throw (vtable unchanged) — `read`/`write`'s internals are **private static members** (friendship to `SipiImage`'s protected state is granted to the class, not the TU); `read_shape`'s is file-local since it needs none. `write`'s override keeps the `kClientAbort` → `SipiImageClientAbortError` branch
- [x] Affected unit tests updated — **none needed**, and that is the check rather than a gap: every PNG test drives an override (`imginfo_test.cpp`, `format_error_path_test.cpp`, the approval encode goldens), so all of them pass with assertions unedited. PNG has no direct-call test of an internal, unlike J2K's palette guard
- [x] Before/after benchmark run recorded — encode `BM_EncodePng` median `−1.95 %` real / `−2.15 %` CPU, moving with a machine-wide `−2 %` drift visible on every untouched case, so no regression. **The decode tier has no PNG case at all**: `@sipi_bench_fixtures` ships no PNG master, and adding one means regenerating and re-releasing a 321 MB pinned archive on `dsp-ci-assets`. Recorded as a deferral for the maintainer rather than worked around

#### Phase 5c: SipiIOJpeg internals (DEV-7061)

- [x] `SipiIOJpeg.cpp` internals converted to `Result`; `setjmp` landing sites return errors instead of throwing — landed as two commits (`read` → `write`), message text byte-identical at all 15 sites. **`read_shape` needed no conversion and got none**: it has no throw sites at all, so a `Result`-returning internal would never carry an error. It picks up its `Result` signature in the Phase 5e vtable flip. `jerr.error_message` stays a `char[]`; `jpegErrorExit` untouched
- [x] `parse_photoshop`/metadata try/catch (`SipiIOJpeg.cpp:670-671`) left in place with a deferral note — it wraps the `Iptc`/`Exif`/`Xmp` constructors that become `Result` factories only in Phase 7, which deletes it. `parse_photoshop` itself was already `static`, so the static-member `read_impl` calls it unchanged
- [x] Override boundary re-wraps `Result` → bool/throw (vtable unchanged) — both internals are **private static members**; `write`'s override keeps the `kClientAbort` → `SipiImageClientAbortError` branch
- [x] Affected unit tests updated — none needed: every JPEG test drives an override, so all pass with assertions unedited
- [x] Before/after benchmark run recorded — the JPEG cases read `+1.6…+2.4 %`, which is **not** a regression: untouched TIFF cases moved further in the same run (`+3.8 %`), the previous session moved the same magnitude the other way, a same-binary A/A control reproduces to ±0.13 %, and the `/1024`-moved-more-than-`/256` shape is the opposite of a per-call-overhead signature. Case (b) under the maintainer's decision rule; no alignment or `copt` hack added

#### Phase 5d: SipiIOTiff internals (DEV-7060)

- [x] `tiffError`/`tiffWarning` handlers re-enabled with format-string safety, on the **real** root cause — a va_list-vs-varargs ABI mismatch in a line that never compiled, not the "malformed UTF-8 segfault" folklore the plan and ADR-0024 both used to repeat. The `va_list` is formatted locally with `vsnprintf` and the result crosses into the logging module as a `%s` **argument**, never as a format string; neither callback may throw into libtiff's C frames. Three call sites that made the handlers ineffective or noisy went with them: both `TIFFSetWarningHandler(nullptr)` landmines (`read`, `read_shape`), `read`'s redundant per-call handler re-installs, and `read`'s unconditional `TIFFTAG_JPEGCOLORMODE` request — a JPEG-codec pseudo-tag that was rejected on every non-JPEG-compressed TIFF, i.e. an error line per decode of the commonest TIFF shape
- [x] `SipiIOTiff.cpp` internals converted to `Result` — landed as four commits (`read_shape` → `read` → the two pixel readers → `write`, plus the bitonal converter); message text byte-identical at all 22 sites. **The libtiff error handler stays a logging side-channel and does not bridge to the error-return**, and that is the correct reading rather than a gap: `SipiIOTiff.cpp` has no `setjmp`/`longjmp` at all, libtiff reports failure through ordinary return codes the call sites already check, so there is no landing site for a global handler to feed. Making a libtiff error reach the per-call `Result` needs libtiff 4.7.1's `TIFFOpenExt` per-handle handlers — recorded as a recommended follow-up in the journal's Deferrals, deliberately not taken here (it moves error text at many sites and needs a multi-error-per-file design call)
- [x] Override boundary re-wraps `Result` → bool/throw (vtable unchanged) — `read`/`write`'s internals are **private static members** (friendship to `SipiImage`'s protected state is granted to the class, not the TU); `read_shape`'s is file-local since it needs none. `readExif`, `writeExif` and `cvrt8BitTo1bit` became `static` to be reachable from the static internals — `SipiIOTiff` carries no instance state, so none of them needed any. `write`'s override keeps the `kClientAbort` → `SipiImageClientAbortError` branch, which the HTTP seam dispatches on to skip Sentry
- [x] Affected unit tests updated — **none needed**, and that is the check rather than a gap: every TIFF test drives an override, so all pass with assertions unedited. Two carve-outs stay exception-based by design (`memTiffOpen`'s raw `malloc` failures and `checked_buf_size_or_throw`, both resource-exhaustion per ADR-0024 Decision 7); `read_watermark`'s six throws belong to `image_processing`'s own migration, and the dead `SipiIOTiff::separateToContig` member is recorded as a side finding
- [x] Before/after benchmark run recorded — decode `OVERALL_GEOMEAN −2.35 %` real / `−2.10 %` CPU with every case negative and the four TIFF cases inside the untouched-JPEG band; encode `−2.00 %` / `−2.25 %` with `BM_EncodeTiff` at `−7.56 %`. No regression on either tier. The `BM_EncodeTiff` mover is favorable and lands on a timed region that contains no read-path work and whose encode success path is provably unchanged, so it is recorded as a binary-layout effect (case (a) of the maintainer's rule) with no alignment/`copt` hack added. See the journal

#### Phase 5e: vtable flip

- [x] `SipiIO` base signatures changed to `Result`-returning; all four overrides drop their wrap layer in the same change — one commit, so the vtable is never in a mixed state. `read` → `Result<bool>`, `read_shape` → `Result<SipiImgInfo>`, `write` → `Result<void>`, plus the four non-virtual convenience `read` overloads. Each handler's `*_impl` is **promoted to be the override itself** rather than left as a forwarder, so no wrap layer survives anywhere
- [x] `SipiImage` dispatcher consumes `Result`, converting to throw at its own boundary — via one file-local `throw_from_value_error` helper. `SipiImage::read`/`read_shape`/`write` keep their current signatures, so **the FFI seam, the Lua bindings and the CLI needed no edit at all**. Behavior preserved exactly: a handler's failure throws *immediately*, before any fallback handler is considered, which is what a throw out of the old override did — the fallback-loop redesign stays Phase 6's. The `kClientAbort` → `SipiImageClientAbortError` dispatch the four handlers each carried now exists in **exactly one place**, `SipiImage::write`
- [x] Full test + approval suite green — `just bazel-test` **73/73**, approval goldens byte-identical. ~20 test call sites and the fuzz harness's two adapted; **no assertion was weakened**, and the `ASSERT_NO_THROW` sites kept both halves (still must not throw, *and* the outcome is checked). Fuzz-harness catch-scope note rewritten per the phase gate: a codec rejecting malformed input now reports a value, so the `try`/`catch` is documented for what genuinely still reaches it (`kdu_exception`, `std::bad_alloc`, the allocation guards, `validate_decode_dims`, the metadata constructors)
- [x] Dedicated before/after benchmark run for the flip itself — flip-only: decode `+1.58 %` / encode `+2.18 %`; but the **cumulative** 5c→5e endpoint comparison is decode `−0.81 %` real / `−0.69 %` CPU and encode `+0.13 %` / `−0.36 %`, i.e. flat. The two intermediate pairs moved ~2 % in *opposite* directions over code whose happy path is semantically unchanged, which is what settles both as session drift rather than an ABI cost. No per-call-overhead signature (a `Result` returned through a virtual would cost *constant absolute* time, so the cheap `/256` cases would move most; they do not). See the journal

#### Phase 6: Hub — Image + image_processing (DEV-7059)

- [x] `Image` construction → `Result`-returning factories per the ADR — **answered by analysis, not by conversion** (`a9c25a1d`), the third time this migration has reached that outcome after `Xmp` and `Icc(PredefinedProfiles)`. Every throw in `src/image/cpp/` construction is a **logic-bug invariant**, which ADR-0024 Decision 7 keeps exception-based permanently. The fact that decides it: the dimensioned constructor `SipiImage(nx, ny, nc, bps, photo)` has **zero production callers** — every construction is in a `*_test.cpp`, and several tests pin its rejections with `EXPECT_THROW`. Its operands are stated by the caller, never decoded from a file, so no input path can reach them. The copy constructor re-derives a buffer size for an image already validated at its own construction, so it can only fire on corrupt internal state (`std::bad_alloc`'s class, the same reasoning that kept `Icc`'s copy constructor throwing), and a constructor cannot return a value anyway. Converting would have added a `Result` factory whose error arm no production caller can produce and **weakened** the `EXPECT_THROW` tests. The header now states the contract so the question does not reopen
- [x] The `image_processing` free functions (extracted in Phase 2) converted to `Result` returns; call sites updated — **all 13 done**, in eight commits over rounds 10–11. The unit that satisfied the maintainer's no-adapter/no-double-touch ruling is **one function (or one tightly-coupled group) plus every one of its call sites, in one commit**, and it made the finale tractable rather than one atomic mega-change: `crop` (`86dd36fd`) → `convertYCC2RGB` (`fe23fbde`) → `removeChannel` + `removeExtraSamples` (`efb4010d`) → `to8bps` (`7acee418`, first CLI seam) → `scale` + `scaleMedium` + `scaleFast` (`de65e5af`) → `add_watermark` (`ebac0ef9`, first HTTP seam) → `convertToIcc` + `toBitonal` (`872f9f39`) → `rotate` + `set_topleft` (`c281379a`). **Grouping was forced by two things the call-site counts do not show**: an internal caller (`set_topleft`→`rotate`, `toBitonal`→`convertToIcc`, `removeExtraSamples`→`removeChannel`) and a shared `switch` at a seam (all three resamplers, and `convertToIcc`/`toBitonal` at the HTTP seam). Both file-local `checked_buf_size_or_throw` helpers are gone; `grep -c throw` over `color.cpp` and `geometry.cpp` is **0**. `crop`'s own last silent-success arm — an unsupported bits/sample that returned `{}` on an untouched image — was closed separately in `2a56f72a`. Remaining, in **dependency-valid** order (these functions call each other, so "smallest first" alone is not a legal order — see the journal's round-11 correction): `convertYCC2RGB` 2, `removeChannel` + `removeExtraSamples` 4 (one commit), `to8bps` 6, `scaleFast` 5, `scaleMedium` 5, `add_watermark` 7, `scale` 13, `convertToIcc` 15 then `toBitonal` 2, `rotate` 37 with `set_topleft` 2 (one commit)
- [x] Friend declarations removed: format handlers switch to the Phase 2 public mutator surface (`pixels_writable()` + metadata setters) — four commits, one per handler, smallest first (PNG `b2fa1946` → JPEG `f286cb1b` → J2K `5c0a1035` → TIFF `280a001e`). `grep "friend class" src/image/cpp/SipiImage.h` is now empty; only the `operator<<` friend remains. Seven members joined the surface, all with callers: `getIptc`, `set_xmp`/`set_iptc`/`set_exif`, `addEs`, `set_geometry`, `getSkipMetadata`. Approval goldens byte-identical after every one; no test assertion edited anywhere. **The reviewable finding is in the JPEG chunk:** the obvious translation of a fill loop — build a local `std::vector`, `set_pixels(std::move(local))` at the end — leaks the whole pixel buffer whenever the fill can `longjmp`, and it passes every test. The buffer is now image-owned across the fill in all four handlers
- [x] `app14_transform` field removed: JPEG handler inverts CMYK/YCCK at decode; downstream sees standard CMYK (ADR-0007) — its own commit (`6ca4a466`), approval goldens byte-identical. The audit's "genuine behavior change" caution proved conservative: the field's whole lifetime was one `SipiIOJpeg::read` call and the inversion already happened there, so downstream *already* saw standard CMYK; what went was the field plus four copy/move propagations. No `volatile` needed — the `setjmp` landing block never reads it
- [x] Temporary `Result`→throw adapter at the facade retired — **MOVED TO PHASE 8 by maintainer ruling (round 10).** Retiring the facade's throwing contract means `SipiImage::read`/`read_shape`/`write` return `Result`, which is Phase 8's caller work by definition; doing it here would touch the same ~50 call sites twice or need boundary adapters, and ADR-0024 Decision 8 says those never ship. Decision 8's own adapter — the per-handler wrap layer — is already gone, retired by the Phase 5e vtable flip. Tracked below in Phase 8; not outstanding here. Original note follows for context: **recommended for a move to Phase 8** (journal Deferrals). Decision 8's own adapter, the per-handler wrap layer, is already gone (retired by the Phase 5e vtable flip). What is left at `SipiImage` is the facade's throwing contract, and retiring *that* means `read`/`read_shape`/`write` return `Result`, which forces Phase 8's first three checkboxes into Phase 6 and would touch the same ~50 call sites twice
- [x] Affected unit tests updated; approval goldens verified unchanged — **complete for Phase 6.** Approval goldens byte-identical after every one of rounds 10–11's commits. No test assertion weakened anywhere; the standard substitution is `ASSERT_NO_THROW(f(...))` → capture the result and assert `has_value()`, because after a conversion the old form passes on a call that returned an error and did nothing — the exact failure mode a stale `ASSERT_NO_THROW` stops catching. Three tests **added** over the two rounds (the unsupported-bps crop rejection, plus round 9's re-enabled `TiffJpegAutoRgbConvert` and the extension-mismatch fallback test), and two strengthened from a bare `EXPECT_FALSE` to an `ErrorCode` assertion. Earlier context (approval goldens byte-identical after every one of round 9's eight commits; no test assertion weakened anywhere; two tests **added** — the re-enabled `TiffJpegAutoRgbConvert` and a new `SipiImage`-level extension-mismatch fallback test). Reopens with the two remaining items
- [x] Before/after benchmark run recorded (transform paths) — **done in round 11**: the whole `image_processing` conversion measured end to end, `86dd36fd` → `c281379a`, `-c opt`, 5 repetitions, both endpoints in the same worktree and session. `OVERALL_GEOMEAN −0.45 %`; every one of the thirteen cases inside the machine's calibrated ±6 % per-case band, so no A/A control was needed. Full table and method in the journal's Phase 6 transform-tier section. Flat is the expected answer: every check this migration adds sits outside the pixel loops. Earlier context: a full three-tier gate ran for round 9's landed half (decode `+0.43 %`, encode `+0.95 %`, process `−0.65 %`, all flat; see the journal's Phase 6 benchmark section and the A/A control that refutes the `+6 %` outlier). The **transform paths specifically** are still untouched, so this reopens with the `image_processing` item — and the process tier's numbers above are that item's genuine "before"

#### Phase 7: Metadata parsers (DEV-7064)

- [x] `Exif` construction → `Result` factory — `Exif::parse` (`615cf7ee`). The default constructor stays public and non-fallible (`SipiImage`'s lazy-init path needs it). `toURational`'s guards and `addKeyVal`'s unsupported-type throw stay exceptions: caller misuse, not malformed input, per ADR-0024 Decision 7. Five call sites, four different policies, all preserved — including PNG's deliberate silent swallow. `read_shape`'s APP1 site turned out to be **unguarded**, so its throw was escaping past a live `jpeg_decompress_struct` and leaking it; still fatal, now destroys the struct and returns the error
- [x] `Iptc` construction → `Result` factory — `Iptc::parse` (`c539a9dd`); the factory decodes into a local and a private constructor takes the already-parsed data, so no `Iptc` exists half-parsed. Message text unchanged. Each of the four call sites keeps the outcome it had, and they **do not agree**: J2K/TIFF/JPEG tolerate a bad block, PNG fails the read. Harmonizing them would change what SIPI serves — recorded as a maintainer decision in the journal's Side findings, not taken here
- [x] `Xmp` construction → `Result` factory — **no factory added, deliberately, and the reason is a finding rather than a shortcut.** `Xmp` construction is *infallible*: all three parsing constructors assign the incoming bytes and hit an unconditional `return;`, and every `throw` in `xmp.cpp` sat in a `/* ... */` block below it — dead code that would not even compile if uncommented (`thisSourceFile` is defined nowhere in the repo). A `Result` whose error arm can never be produced is an abstraction with no caller. The commit (`55a6118c`) deletes the dead blocks, the orphaned `Exiv2::XmpData` member and both exiv2 includes, and states the enduring fact instead: SIPI holds the XMP packet as opaque RDF/XML bytes, neither parsing nor validating it
- [x] `Icc` construction → `Result` factory (ICC determinism invariant: all emission still funnels through `iccBytes()`) — three factories (`parse`, `createFromProfile`, `createRGB`), not five (`37d4bef5`). `Icc(PredefinedProfiles)` stays a throwing constructor because its only throws are the two caller-misuse invariants (no input-parse failure to convert), and the copy constructor stays because `SipiImage`'s copy constructor/assignment call it and cannot return a `Result` — its failure is an already-validated profile failing an lcms2 round-trip, which belongs with `std::bad_alloc`. `createRGB` gained a genuine error arm: `cmsCreateRGBProfileTHR`'s return was stored unchecked, so a null propagated as an `Icc` wrapping nothing. `iccBytes()`/`iccFormatter()` are **emission**, deliberately left throwing and out of this phase's scope; approval goldens byte-identical confirms the determinism chokepoint is intact
- [x] Handler call sites updated; the Phase 5c try/catch fully deleted — and **three more went with it** (`14b6d89d`). All four `catch (const std::exception &)` blocks on the JPEG metadata paths are gone: both around `parse_photoshop` and both around the APP1 XMP extraction, in `read` and `read_shape` each. The reachability argument is in the commit body; the short form is that after the factories, only `std::bad_alloc` could reach them, and catching *that* here would be actively wrong — ADR-0024 keeps it exception-based for the seams, so the block would have turned an OOM into a logged warning. `grep -c "catch (const std::exception" src/format_handlers/cpp/SipiIOJpeg.cpp` is now `0`; the three surviving catches are specific (`std::out_of_range`/`std::invalid_argument` around the quality `stoi`, `SipiError` on the write path)
- [x] Affected unit tests updated — **none needed**, and that is the check rather than a gap: every metadata test drives a handler, so all pass with assertions unedited. Worth recording that at this point **no test in the repo covered a malformed IPTC, EXIF or ICC block in any format** — closed at the end of the plan, once the tolerate-vs-fatal policy question was settled, by three crafted fixtures (`jpeg_exif_truncated.jpg` `75456d94`, `tiff_icc_garbage.tif` `df40e27c`, `j2k_exif_truncated.jp2` `7d0c7258`), each of which reaches a parser and fails there rather than at a bounds guard, and each of which also joins its format's fuzz seed corpus
- [x] Before/after `decode_benchmark` run recorded (metadata construction runs inside `read()` — hot-path rule applies) — before = `decode-p6-after.json` at `c2bbbd13`, after = `decode-p7-after.json` at `14b6d89d`; `-c opt`, 20 repetitions, same machine and session. **`OVERALL_GEOMEAN +0.12 % real / +0.02 % CPU`** — flat. Every one of the 18 cases lands within ±2.5 % (largest movers `decode_tile/jp2/256` at `−2.45 %` and `decode_tile/flat_tiff/1024` at `+1.75 %`), so nothing approaches this machine's calibrated ±6 % per-case / ±2 % geomean drift band and no A/A control was needed. No regression

#### Phase 8: Seams + retirement (DEV-7065)

- [x] `SipiImage::read_shape` returns `Result<SipiImgInfo>` — `8c2d230a`, with all three of its call sites (all on the FFI seam). Split out of the facade item below because it landed on its own: it is by far the smallest of the four members (3 call sites against ~144 for the rest), which made it the right proof that the facade decomposes **by member**, exactly as Phase 6 decomposed by function. The dispatch and fallback loop stopped flattening a handler's `Result` into an exception via `throw_from_value_error` and now propagate it. `Sipi::InfoError` was deleted with it — an enum nothing ever threw, whose `catch` arm and `"Couldn't get dimensions"` message were unreachable
- [x] `SipiImage::read` and both `readSource` overloads return `Result<void>` — `6424da18`, with **all** of their call sites (7 production files, ~29 test and benchmark files). Split from the `write` item below because the two are independent members; `read` and `readSource` had to convert together because `readSource` is implemented in terms of `read`, exactly as round 11 predicted. Landing it required one prerequisite the plan did not list: `validate_decode_dims` (`src/image/cpp/SipiIO.h`) still **threw** on a header-derived rejection, which would have escaped the converted facade — converted first, in `a333c496`
- [x] Invariant catches kept at the seams, not deleted with the decode arms — `6424da18`. `read` no longer throws *decode failures*, but the allocation guards (`checked_buf_size_or_throw`), `memTiffOpen`'s raw `malloc` and Kakadu's `kdu_exception` still throw, so `serve_image.cpp`'s `SipiImageError` arm and the `std::exception` arms in `verify.cpp` / `convert_service_file.cpp` remain, each now commented with what it actually covers. This is the plan's "reduced to `std::bad_alloc` + invariant catches" phrasing read strictly: *reduced*, not removed
- [x] `SipiImage::write` returns `Result<void>` — `374a68b1`, with all ~50 call sites. `throw_from_value_error` died with it, as predicted, and so did **`SipiImageClientAbortError`**: an exception class with one thrower and one catcher, whose only function was to carry `ErrorCode::kClientAbort` across a `throw`. The seam now reads the code off the value. The facade is throw-free on every fallible path; only `io.at(ftype)`'s `std::out_of_range` (unknown format string = programming error) and the allocation guards remain. Original item text follows: The rest of the facade's throwing contract retired: `SipiImage::write` returns `Result` (moved here from Phase 6 by maintainer ruling — its call-site work is the three checkboxes below, so it lands with them, not before them). **Scoped in round 11:** ~144 remaining call sites, of which **22 are production** (`cli_app.cpp` 7, `image_handle.cpp` 5, `serve_image.cpp` 5, `convert_access_file.cpp` 2, `convert_service_file.cpp` 2, `verify.cpp` 1) and ~125 are tests and benchmarks. **Decompose by member, one member plus all its callers per commit** — but verify the dependency order first (`readSource` looks to be implemented in terms of `read`); round 11's first correction was that a naive smallest-first order was illegal for exactly this reason. `throw_from_value_error` exists only to serve this contract and should die with it, not before
- [x] `src/ffi/serve_image.cpp` consumes `Result` directly — `6424da18` (read) + `374a68b1` (write) + `aec2c8a0` (the dead guard). The try/catch is reduced to exactly what can still fire: `std::bad_alloc`, the `SipiImageError` invariant arm (allocation guards, `memTiffOpen`, Kakadu) and the `SipiSizeError` 400 arm. **The reduction is smaller than the phrasing suggests, and that is the finding**: converting a callee does not stop it throwing — everything under it that is invariant-class still does — so a catch arm is deleted only when nothing beneath the call can reach it. The one thing that *did* become unreachable, `info.success == FAILURE` after the shape probe, was proved unreachable path by path and then deleted
- [x] `src/ffi/image_handle.cpp` (Lua surface) consumes `Result` directly — `6424da18` (read/readSource) + `374a68b1` (write). **Lua strings verified unchanged by construction**: every arm that emitted `SipiImageError::message()` now emits `SipiValueError::client_message()`, and the two are the same function — path-redacted, no source location (`sipi_value_error_test.cpp` pins it). The `Sipi::SipiError`, `std::exception` and `...` arms stay: `sipi_image_new` still constructs `SipiRegion`/`SipiSize` from Lua-supplied strings, which throw
- [x] CLI verbs (`cli_app.cpp`, `commands/verify.cpp`, `commands/convert_*.cpp`) consume `Result` — `6424da18` + `374a68b1`. Exit codes unchanged (`EXIT_FAILURE` everywhere, per the two-code convention `error-model.md` was corrected to state in round 11); stderr text unchanged, `diagnostic_message()` being `to_string()`'s equivalent. `query` and `compare` had no local handler and relied on the `extern "C"` catch-all, so their new value arms reproduce **that** message verbatim, including its "unhandled exception" wording — preserved deliberately rather than improved, since the text is user-visible and this round is a mechanism change
- [x] `SipiImageError` reduced to the truly-unrecoverable role; subclassing-pattern docstring updated — `450c312c`. **Reduced, not removed**, and the ADR's own split says why: the allocation-overflow guard, `memTiffOpen`'s `malloc` failures, the geometry invariants and the pixel accessors are programming errors with no input path, and a constructor cannot return a value. Its one subclass (`SipiImageClientAbortError`) was deleted in `374a68b1`. The docstring had been teaching a subclass-for-policy pattern that is now false in every particular — policy is `policy_for(ErrorCode)` data, there is no type-ordered `catch` chain, and fallible operations do not throw
- [x] Remaining throwing convenience wrappers removed — `450c312c` (`getPixel`/`setPixel` stopped throwing bare `int`s; they keep throwing `SipiImageError` because **neither has a production caller** and their arguments never trace to input, the same test `11I` used on the dimensioned constructor) + `374a68b1` (`throw_from_value_error`) + `4bcc5d68` (three callerless arithmetic operators, deleted rather than converted)
- [x] Fuzz harness catch blocks narrowed to the post-migration contract — `9e3c8f69`. **Both arms survive the narrowing**, and the value of the item turned out to be the audit rather than the deletion: the comment justifying them listed metadata constructors that no longer throw (EXIF/IPTC/ICC are `Result` factories; `Xmp` stores bytes verbatim and never parsed them), while what *does* still throw — `kdu_exception` from the Kakadu accessors the J2K path does not individually guard, plus the allocation guards — was under-stated. Deleting a reachable arm would have turned a real crash into a silent pass. The two `static_cast<void>` discards became checked `if (const auto r = …; !r)`, since a `(void)`-cast is exactly what `CONVENTIONS.md` warns defeats `[[nodiscard]]`
- [x] CONVENTIONS.md rule (or reviewer-guidelines entry) added: new fallible ops in the image layer return `Result`, `[[nodiscard]]` mandatory; cite clang-tidy `bugprone-unused-return-value` (covers `std::expected` by default, catches `(void)`-casts that `[[nodiscard]]` misses) — landed in `8a3cefde`, in `CONVENTIONS.md`'s existing "Error Handling Pattern" section rather than a new one. The clang-tidy citation carries its own justification: the `(void)`-cast property is precisely what `[[nodiscard]]` alone misses, and a discarded result is how the server served a full-size image in place of a requested crop
- [x] `UBIQUITOUS_LANGUAGE.md` / docs updated where error-model terms changed — `8a3cefde`. **The glossary is a verified no-op**: `Result`, `SipiValueError`, `ErrorCode`, `client_message`/`diagnostic_message` are mechanism-level vocabulary, and ADR-0024 designates `error-model.md` as their living catalogue; the glossary's one adjacent entry (`SipiImageClientAbortError`, under "Client abort") is still accurate. What *did* need fixing was `error-model.md` itself, which stated the CLI derives its exit code from `HttpStatusClass` with distinct non-zero codes. It does not — all five verbs return only `EXIT_SUCCESS`/`EXIT_FAILURE`, verified file by file — so the seam description now says what the code does and marks the richer mapping unimplemented

### Post-migration follow-ups (this PR)

Maintainer-authorized after reviewing the open questions and the first manual
fuzz run (`fuzz.yml` workflow_dispatch, run 33254456688). Everything here lands
on the same branch as the eight phases above.

- [x] **DEV-7078** — heap-buffer-overflow in `Icc::parse` on a profile with no description tag: `cmsGetProfileInfoASCII` returns `0`, `make_unique<char[]>(0)` yields a never-NUL-terminated buffer, and the `strcmp`/`strncmp` classification reads past it. Found by the ASan-paired JPEG fuzz leg; reachable from every codec that parses an embedded ICC profile. Standalone `fix(metadata):` (the bug is on `main`), with a co-located unit test over a description-less profile — `acdeffcd`. The crash input's profile carries a tag count of literally `0`; the guard makes an absent description `icc_unknown`, which is what the fall-through arm already produced for every unrecognised description, so no classification outcome moved and the goldens are byte-identical
- [x] The JPEG crash reproducer pinned into the seed corpus as a malformed fixture — `5aa6f54d`. Not the 8 KB fuzzer artifact but its *input class*, regenerated by the committed fixture generator: `jpeg_icc_no_description.jpg` (1199 bytes) carries the real `ICC_PROFILE\0` + sequence/count APP2 layout so the bytes clear every upstream guard and reach `Icc::parse`, wrapping a header-only profile with a tag count of zero
- [x] Fuzz-config triage — Kakadu's UBSan noise under `--config=fuzz`, the J2K libFuzzer timeout, the TIFF libFuzzer OOM, and the pre-existing `parse_request` ASan report, each classified bug-or-config with its disposition recorded in the journal — `cfb1d7d8` (Kakadu's `signed-integer-overflow`, a *fourth* subcheck the existing exclusion list did not cover, and reachable from `read_shape` rather than only from the multithreaded decode) + `b55e704c` (the TIFF OOM answered with a harness decode-size budget, because production bounds that allocation at the seam and no `-rss_limit_mb` can bound a crafted claim). The `parse_request` ASan report is in libFuzzer's own `Sha1ToString`, not in SIPI code — its own issue. **The J2K timeout is a real finding and is the round's blocker**: a 5745-byte JP2 with a zero-length, zero-type child inside `jp2h` spins Kakadu's box walk for at least 293 s of CPU inside `read_shape`, and the loop is in third-party code SIPI cannot patch (journal § Side findings)
- [x] **Q5** — `Icc::createRGB`'s synthesis failure made fatal at its TIFF call site, for consistency with the metadata-integrity rule (a file whose colour tags cannot form a profile is corrupt) — `935ebae8`. Two sites, both in `read`'s `TIFFTAG_WHITEPOINT` branch; the J2K equivalent already propagated. `error-model.md`'s claim that synthesis is "unaffected by this contract" was the last false statement about the metadata rule and is gone. Goldens byte-identical: `createRGB` fails only when lcms2 cannot build a profile from the given tags, which no fixture triggers
- [x] **Q1** — libtiff diagnostics captured per call through `TIFFOpenExt` + `TIFFOpenOptionsSetErrorHandlerExtR` and carried in the `Result`, all four `TIFFOpen` sites; multiple errors for one file are **concatenated** into one message (maintainer ruling) — `d0c633d6`. Verified end to end (`TIFFopen of "…" failed!: TIFFOpen: …: No such file or directory`). Two decisions the ruling did not cover, both taken deliberately: warnings are captured alongside errors, because libtiff reports most malformed-directory conditions at *warning* level while the `TIFFGetField` they poison merely returns 0; and the handlers return "not handled" so the process-global `log_err`/`log_warn` still sees everything. The `nullptr`-open paths in `read`/`read_shape`/`read_watermark` keep returning "not this format" instead of an error — that is the format-dispatch fallback contract, so the highest-value libtiff message ("bad magic number") reaches the log but not the `Result` on the read paths
- [x] **Q3** — 16-bit watermarking implemented (`add_watermark`'s `bps == 16` arm is a silent no-op today), with a test over a 16-bit image — `7471e781`. It did generalize exactly as the ruling assumed: the watermark file is always 8bps (`read_watermark` rejects any other depth), so only the image side changes scale. The test that already covered this path asserted only `has_value()`, which the no-op satisfied — it now reads an unwatermarked reference and proves the pixels moved by a bounded amount with the geometry unchanged
- [x] **DEV-7078, second instance** — the same zero-length-description read in `operator<<(std::ostream &, Icc &)`, four times over (description, manufacturer, model, copyright). Worse than the original because those three tags are optional and routinely absent from ordinary profiles, and the operator is reachable from `sipi query` and from Lua's `tostring(img)` — `e962e15a`. Found by grepping the rest of `icc.cpp` after the first fix rather than by the fuzzer, which cannot reach a stream operator. The two `cmsSaveProfileToMem` sizing idioms in the copy constructor and `createFromProfile` were checked and are **not** instances: they pass the buffer to `cmsOpenProfileFromMem` with an explicit length, never read it as a string
- [x] **Q4** — the four-channel PNG encode geometry investigated against a crafted fixture and either fixed (real over-read) or normalized and documented (cosmetic bookkeeping) — `62511be1`, **cosmetic**: `convertToIcc` ends in `set_pixels`, the single size-checked path that moves buffer and geometry together, so the following `set_geometry(nx, ny, 3, 8)` re-stated values already set. Removed, with the reason stated in its place
- [x] **PNG metadata round-trip** — SIPI's PNG writer put raw binary EXIF/IPTC into a libpng *text* chunk, cut at the first NUL, so a PNG SIPI wrote could not be read back once unparseable PNG EXIF became fatal — `17f69cef`. EXIF now rides the binary-safe `eXIf` chunk (`png_set_eXIf_1`/`png_get_eXIf_1`); IPTC uses the ImageMagick `Raw profile type iptc` hex-text convention (NUL-free, exiftool/IM-interoperable); empty EXIF/IPTC emits no chunk; and the redundant `png_write_info`/`png_write_end` around `png_write_png` (which duplicated `eXIf` and the trailing `IEND` in *every* PNG SIPI wrote) is removed. Round-trip verified end to end: `sipi convert cmyk.tif out.png -F png && sipi convert out.png rt.tif` now succeeds, and a colocated `PngMetadataRoundTrip` test proves EXIF+IPTC survive write→read byte-for-byte for `palette.tif`. Three PNG approval goldens moved (metadata chunks + single `IEND`; `IDAT` pixel streams byte-identical), recorded in `test/approval/CHANGELOG.approval.md`. **Side finding:** `Iptc::iptcBytes()` (Exiv2 `IptcParser::encode`) returns 0 bytes for a TIFF carrying only a lone `ApplicationRecordVersion` IPTC dataset (`cmyk.tif`, `cielab.tif`), so that IPTC does not round-trip through *any* carrier — a pre-existing metadata-layer issue, not the PNG bug; follow-up candidate

## Acceptance Criteria

- [x] Codec fuzz targets for TIFF/JPEG/PNG/J2K exist, corpus-seeded, running nightly and as corpus-replay in the `//src/...` sweeps (DEV-7066 "done when") — `bc3cac6a` + `9b01a82e`; verified at round 12's close: the four `//src/format_handlers/fuzz:*_decode_fuzz` targets are in `bazel query 'tests(//src/...)'` (so every `just bazel-test` replays them) and each has its own leg in `fuzz.yml`'s matrix
- [x] The `parse_photoshop`-overshoot and J2K dimension-overflow inputs are in the corpus and replay clean — `images/malformed/jpeg_photoshop_overshoot_app13.jpg` is in `//test/_test_data:fuzz_seeds_jpeg` and `images/malformed/j2k_oversized_dimensions.jp2` in `:fuzz_seeds_j2k`, both reaching the harnesses through their packages' `seed_corpus` filegroups; green in every `just bazel-test` this round
- [x] After Phase 8: no `throw SipiImageError` remains on a fallible-op path in `src/format_handlers/`, `src/image/` + `src/image_processing/`, `src/metadata/`; exceptions remain only for OOM/invariants — **verified by grep at round 12's end.** What the grep returns, and why each stays: `memTiffOpen`'s two raw `malloc` failures and `read_watermark`'s buffer allocation (OOM class), `checked_buf_size_or_throw` in `SipiImage.cpp` / `SipiIOTiff.cpp` / `compose.cpp` (allocation-size overflow invariant), `SipiImage`'s construction and `set_pixels` geometry invariants (ADR-0024 Decision 7; no production caller reaches them), and `getPixel`/`setPixel`'s coordinate checks (same test, documented in the header). `src/metadata` throws only `SipiError` from `Icc`'s profile *emission*, exempt under the ICC-determinism invariant, and from `exif.h`'s `addKeyVal` type dispatch. Every one is invariant- or OOM-class; none is on a fallible path
- [x] Layout convention holds (verified at round 12's close: no `src/*-rs` directory, no loose source at the `src/` root, no `engine/` or `formats/` package): no `-rs`-suffixed folders remain; every `src/` package organizes sources under `cpp/` and/or `rust/`; the hub is dissolved into `//src/image` + `//src/image_processing` + `//src/cache` + `//src/error` with no loose sources at the `src/` root and no `engine/` folder; `//src/format_handlers` replaces `//src/formats`
- [x] Every phase lands with `just bazel-rustfmt-check` / `bazel-clippy-check` (where Rust is touched) and the full test suite green; approval goldens unchanged throughout — `just bazel-test` **73/73** after every commit of round 12, both lint gates clean at its close (no Rust was touched this round), and the approval goldens are byte-identical throughout. **Still outstanding for the branch as a whole: a Linux CI verdict.** The branch is 90+ commits deep and a green macOS sweep cannot see a Linux-gated target
- [x] Each handler migration justified by a before/after benchmark run per the hot-path rule — per-phase gates in rounds 6–11 (J2K, PNG, JPEG, TIFF internals, the vtable flip, the transform tier), plus round 12's endpoint run over all three tiers (`8c2d230a` → `5a8914e1`): decode geomean **+0.26 %**, encode **−1.78 %**, process **+0.03 %**, every case inside the machine's ±6 % per-case band. Flat is the expected answer, not a lucky one: every check this migration added sits outside a pixel loop, and the facade's `Result` return happens once per request
- [x] HTTP status mapping, Sentry reporting, metrics, and CLI exit codes are behavior-identical before and after (verified per phase) — **with three deliberate, maintainer-ordered exceptions**, all `fix:` commits because they correct behavior on `main`: a one-pixel IIIF size is rejected instead of answered with the full-size image (`da0914f7`) and answers **400** through the new `kInvalidRequestParameter` / `HttpStatusClass::kClientError` pair, which is also the first code whose status class is not `kInternalError` (`8e9609c1`); and a file whose embedded metadata does not parse is refused instead of served without it, in all four handlers (`5a8914e1`, PNG's EXIF arm in `1fe886e6`). Everything else holds: the client-abort path still skips Sentry and increments `client_disconnected_total`, `report_value_error` reproduces `report_image_error`'s payload, Lua strings are `client_message()` (identical to the old `message()`), and every CLI verb still returns `EXIT_SUCCESS`/`EXIT_FAILURE` with unchanged stderr text

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
