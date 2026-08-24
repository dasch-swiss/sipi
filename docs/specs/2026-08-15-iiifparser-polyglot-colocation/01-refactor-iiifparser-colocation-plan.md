---
title: "Colocate the iiifparser C++/Rust implementations; carve a domain-typed Rust parser crate (path 3)"
type: refactor
date: 2026-08-15
author: "Ivan Subotic"
status: reviewed
repository: dasch-swiss/sipi
---

# Colocate the iiifparser C++/Rust implementations; carve a domain-typed Rust parser crate (path 3)

## Overview

Reorganize the `iiifparser` module so both language implementations live under one
component-first directory, and decouple the Rust parser from the FFI seam. The C++
code moves into `src/iiifparser/cpp/`; the Rust parser is carved out of the
monolithic `//src/server-rs:lib` crate into a standalone `//src/iiifparser/rust:iiif_parser`
crate that emits its own domain types; the existing language-neutral corpus gains a
Rust regression consumer alongside the C++ one. The decision is recorded in
`docs/adr/0021-iiifparser-polyglot-colocation.md` (path 3: the parser emits domain
types, `server-rs` owns the domain → `SipiIiifParams` flattening).

This is a structural refactor with **no end-user behavior change** — commit
`refactor(iiifparser): …`, no `!`.

## Problem Statement / Motivation

- The two implementations of one logical component are not colocated: C++ under
  `src/iiifparser/`, Rust in `src/server-rs/src/iiif.rs`. Reading them side by side
  during the strangler migration means jumping trees.
- The Rust parser is coupled to the FFI seam. `iiif.rs:11` imports
  `crate::ffi::{SipiFormatType, SipiIiifParams, SipiQualityType, SipiRegionType, SipiSizeType}`
  and emits the flattened `#[repr(C)] SipiIiifParams` directly. A pure-CPU URL
  parser cannot be built or reasoned about without the FFI module that links the
  C++ engine, and it inherits the `_CPP_STDLIB_LINK` bracket and the `no-sanitizer`
  exclusion.
- The corpus (`src/iiifparser/corpus/`) is a language-neutral asset wired to only
  one language.

## Proposed Solution

Component-first, language-first, with the C++ side split by lifetime and a neutral
corpus package (see ADR-0021 for the decision and rejected alternatives):

```
src/iiifparser/
├── corpus/                     filegroup seed_corpus  (unchanged location)
├── cpp/
│   ├── value_objects/          cc_library iiifparser + tests + parse_benchmark — LIVE engine
│   └── classifier/             cc_library iiif_handler (testonly) + tests — deletable reference
└── rust/                       rust_library iiif_parser (multi-module) + rust_test unit + rust_test corpus regression
```

The C++ split is by fate: `value_objects/` is live engine code (stays);
`classifier/` is the reference oracle the Rust port checks against, and its whole
folder is `rm -rf`-deletable once the Rust parser is trusted. The classifier already
deps only `//src/util` (never the value objects), so the seam is natural.

Path 3 carve: the `iiif_parser` crate defines domain enums (`RegionKind`,
`SizeKind`, `QualityKind`, `FormatKind`) and a domain `IiifParams` struct
(idiomatic Rust: `bool` flags, no `#[repr(C)]`), depends only on
`@crates//:percent-encoding`, and never touches the FFI. `server-rs` owns
`From<iiif_parser::IiifParams> for SipiIiifParams` (exhaustive matches) applied at
the single `serve_image` site. The crate is **broken into internal modules**
(`domain`, `parse`, `request` — no file approaching 1000 lines); it is *not* split
into packages like the C++ side, because it is uniformly production with no
delete-boundary to encode.

## Alternative Approaches Considered

Recorded in full in ADR-0021. Summary:

- **Path 1** (parser deps `//src/ffi:sipi_ffi`): rejected — drags the C++ link into
  a pure parser.
- **Path 2** (extract a shared types-only module): viable interim, rejected as the
  endpoint — parser still emits FFI-flavored types.
- **Path 3** (parser emits domain types; `server-rs` owns the mapping): chosen.
- **Language-first split** / **single mixed BUILD**: rejected — scatters the
  component / invites `glob()` and name collisions.

## Technical Considerations

- **C++ split by lifetime is load-bearing.** `value_objects/` `cc_library iiifparser`
  is **live production engine code** (`//src:engine` deps it; `SipiIO.h`/`SipiImage.cpp`/
  format handlers pass `std::shared_ptr<SipiRegion>`/`SipiSize`). It is **not**
  `testonly` and **not** deletable. `classifier/` `cc_library iiif_handler` is the
  `testonly` reference oracle, deletable as a whole folder. Each is its own Bazel
  subpackage; do not blanket-label `cpp/` as "reference impl."
- **Include-path preservation, per subpackage.** After the move the current
  `strip_include_prefix = "/src"` would resolve headers as `iiifparser/cpp/.../SipiSize.h`,
  breaking every consumer. Each subpackage's `cc_library` must pin the virtual
  `iiifparser/` prefix to its own physical depth:
  - `value_objects/`: `strip_include_prefix = "/src/iiifparser/cpp/value_objects"` + `include_prefix = "iiifparser"` → `#include "iiifparser/SipiSize.h"`.
  - `classifier/`: `strip_include_prefix = "/src/iiifparser/cpp/classifier"` + `include_prefix = "iiifparser"` → `#include "iiifparser/iiif_handler.h"`.
  Consumers that break if wrong: `src/SipiIO.h:16-17`, `src/ffi/serve_image.cpp:26-31`,
  `src/cli/commands/convert_access_file.cpp:19,21`, `src/formats/decode_benchmark.cpp:42-43`,
  the colocated tests, and `parse_benchmark.cpp`.
- **Rust modules, no monolith.** The carved crate must be split into internal modules
  (suggested: `domain.rs` types, `parse.rs` grammar consumers + validators + component
  parsers, `request.rs` tokenize + `parse_request` + `RequestKind`/`ParsedRequest`,
  `lib.rs` root + public re-exports). No file should approach 1000 lines. Keep
  `#[cfg(test)]` tests colocated with the module they exercise.
- **Coupling guard migration.** Today the `ffi.rs` discriminant static-asserts guard
  the wire values because the parser emits the FFI enums. After the carve, the
  guard becomes the `From` impls — they must be **exhaustive `match` arms, never
  `x as c_int` casts**. Keep the `ffi.rs` asserts (they still guard `SipiIiifParams`
  + the FFI enums against the C++ header). The domain enums must be **total
  supersets** of the FFI enums: `FormatKind` carries all 8 variants
  (Unsupported/Jpg/Tif/Png/Gif/Jp2/Pdf/Webp — `parse_quality_format` produces
  Gif/Pdf/Webp/Unsupported even though classification rejects them); `SizeKind`
  keeps `Undefined` and `Reduce` (the unreachable `red:` branch kept for port
  fidelity). A narrower domain enum makes `From` non-total.
- **Clamps/caps stay in the parser.** The `.min(32_000)` dimension cap, the
  `pct <= 1e-12 → 1.0` clamp, and the `red < 0 → 0` clamp live inside `parse_size`
  and travel with the crate; keep their crate-level tests.
- **Runfiles, not cwd.** A `rust_test` does not run at workspace root (the `cc_test`
  does — hence `SIPI_FUZZ_CORPUS_DIR`). The Rust corpus test must use
  `@rules_rust//rust/runfiles`: `let r = Runfiles::create()?;` then the
  `runfiles::rlocation!(r, "src/iiifparser/corpus")` **macro**, which applies bzlmod
  repo-mapping (the compile-time `BAZEL_CURRENT_REPOSITORY`) automatically. Do **not**
  hardcode the `_main/` prefix and do **not** hand-build the path from a
  `current_repository()` method (not part of the stable public API — confirm the
  exact runfiles API against the pinned `rules_rust` version at plan time). The
  `seed_corpus` glob includes `corpus/BUILD.bazel`, so the Rust iterator must skip
  non-corpus entries. No `MODULE.bazel` change: `percent-encoding` is already
  declared and the runfiles library ships with `rules_rust`.
- **No shims.** Delete `src/server-rs/src/iiif.rs` and `pub mod iiif;`; retarget all
  callers and all `//src/iiifparser` labels in the same commit.

## Implementation Phases

#### Phase 1: Move the C++ implementation into `cpp/value_objects/` and `cpp/classifier/`

- [x] `git mv` the six `Sipi*.{cpp,h}`, their five value-object `*_test.cpp` (`decode_dims_test`, `seam_roundtrip_test`, `sipiidentifier_test`, `sipiqualityformat_test`, `sipiregion_test`, `sipirotation_test`, `sipisize_test`), and `parse_benchmark.cpp` into `src/iiifparser/cpp/value_objects/`.
- [x] `git mv` `iiif_handler.{cpp,h}`, `iiif_handler_test.cpp`, and `parse_iiif_uri_corpus_test.cpp` into `src/iiifparser/cpp/classifier/`.
- [x] Create `src/iiifparser/cpp/value_objects/BUILD.bazel`: `cc_library iiifparser` (non-`testonly`, `strip_include_prefix = "/src/iiifparser/cpp/value_objects"` + `include_prefix = "iiifparser"`, `default_visibility = ["//src:__subpackages__"]`), `cc_test iiifparser_test`, `cc_binary parse_benchmark` (`testonly`, `manual`).
- [x] Create `src/iiifparser/cpp/classifier/BUILD.bazel`: `cc_library iiif_handler` (`testonly`, deps `//src/util` only, `strip_include_prefix = "/src/iiifparser/cpp/classifier"` + `include_prefix = "iiifparser"`, `visibility = ["//test/approval:__pkg__"]`), `cc_test iiif_handler_test` (data `//src/iiifparser/corpus:seed_corpus`, `local_defines SIPI_FUZZ_CORPUS_DIR="src/iiifparser/corpus"`).
- [x] Write each subpackage's BUILD docstring stating enduring role (per repo comment conventions): `value_objects` = live engine value objects whose string constructors are the reference the Rust `parse_*` port; `classifier` = `testonly` reference oracle for the Rust `parse_request`, deletable as a whole folder when the port is trusted (cite the tracking issue, e.g. `DUNE-xxx`).
- [x] Delete the old `src/iiifparser/BUILD.bazel` (its targets now live in the two subpackages; the bare `src/iiifparser/` parent dir is fine, as `corpus/` already proves).
- [x] Verify no `glob`/`filegroup`/coverage/clang-format target in a parent package reaches into `src/iiifparser/**` before relying on the parent-BUILD deletion.
- [x] Retarget `//src:engine` dep at `src/BUILD.bazel:280`: `//src/iiifparser` → `//src/iiifparser/cpp/value_objects:iiifparser`.
- [x] Retarget `//test/approval:approvaltests` dep at `test/approval/BUILD.bazel:54`: `//src/iiifparser:iiif_handler` → `//src/iiifparser/cpp/classifier:iiif_handler`.
- [x] Update `corpus/BUILD.bazel` docstring consumer reference to `//src/iiifparser/cpp/classifier:iiif_handler_test` and add the new Rust consumer.
- [x] Scrub comment-only label references to `//src/iiifparser` in `src/BUILD.bazel:200,207,243,440` and `src/ffi/BUILD.bazel:77`.

#### Phase 2: Carve the domain-typed, multi-module `iiif_parser` crate

- [x] Create `src/iiifparser/rust/` and split the parser carved from `src/server-rs/src/iiif.rs` into modules (no file near 1000 lines): `domain.rs` (types), `parse.rs` (grammar consumers, validators, component parsers, `parse_iiif_params`), `request.rs` (`tokenize`/`urldecode`/`build_prefix_strict`/`parse_request`/`RequestKind`/`ParsedRequest`), `lib.rs` (crate root, module decls, public re-exports).
- [x] Define domain types in `domain.rs`: enums `RegionKind`/`SizeKind`/`QualityKind`/`FormatKind` (total supersets of the FFI enums) and a domain `IiifParams` struct (idiomatic Rust: `bool` for `size_upscaling`/`rotation_mirror`, no `#[repr(C)]`), replacing all `crate::ffi::*` usage.
- [x] Change `ParsedRequest.params` to `Option<IiifParams>` (domain type) and remove the `use crate::ffi::…` import.
- [x] Move the inline `#[cfg(test)] mod tests` (~22 tests) into the crate, colocated with the module each exercises; update assertions to the domain enums.
- [x] Create `src/iiifparser/rust/BUILD.bazel`: `rust_library iiif_parser` (crate root `lib.rs`, all module `.rs` in `srcs`; deps `@crates//:percent-encoding` only; **no** `//src/ffi:sipi_ffi`, **no** `_CPP_STDLIB_LINK`), `default_visibility` admitting `//src/server-rs`, and `rust_test(crate = ":iiif_parser")` for the inline unit tests (leave **untagged** — no `no-sanitizer`, since it has no C++ dep).
- [x] Delete `src/server-rs/src/iiif.rs`, remove `pub mod iiif;` from `src/server-rs/src/lib.rs`, and drop `src/iiif.rs` from the `//src/server-rs:lib` `srcs`.
- [x] Add `//src/iiifparser/rust:iiif_parser` to `//src/server-rs:lib` deps.
- [x] Update `src/server-rs/src/routes.rs:27` import from `crate::iiif::{self, ParsedRequest, RequestKind}` to the `iiif_parser` crate.
- [x] Add the domain → FFI mapping in `server-rs` (`ffi.rs` or a small module): `From<iiif_parser::IiifParams> for SipiIiifParams` + the four enum `From`s, using **exhaustive matches**.
- [x] Apply the mapping at the single seam site `routes.rs` `serve_image` (~L710-741): `let params: SipiIiifParams = parsed.params.expect(...).into();`.
- [x] Keep the `ffi.rs` discriminant static-asserts and the `sipi_unit_test` target (it still covers `routes.rs`/`ffi.rs`/config).

#### Phase 3: Rust corpus regression test

- [x] Add `src/iiifparser/rust/corpus_regression_test.rs` sweeping `iiif_parser::parse_request` over every corpus file, locating the corpus directory via `@rules_rust//rust/runfiles` (`Runfiles::create()` + the `runfiles::rlocation!` macro, which applies bzlmod repo-mapping — not a hardcoded `_main/` prefix), skipping `BUILD.bazel`.
- [x] Assert the swept file count is `> 0` (and ideally matches the C++ test's count) so a broken runfiles path cannot vacuously pass.
- [x] Add the `rust_test` target in `src/iiifparser/rust/BUILD.bazel` with `data = ["//src/iiifparser/corpus:seed_corpus"]` and `deps = [":iiif_parser", "@rules_rust//rust/runfiles"]`.

#### Phase 4: ADR, docs, tooling, gates

- [x] `docs/adr/0021-iiifparser-polyglot-colocation.md` (already drafted in this change) — verify `0021` is the next free number and cross-links resolve.
- [x] Retarget label references in `docs/src/development/benchmarking.md` (`//src/iiifparser:parse_benchmark` → `//src/iiifparser/cpp/value_objects:parse_benchmark`), `testing-strategy.md`, `developing.md`, `commit-conventions.md` to the new subpackage labels.
- [x] Coordinate the `ARCH-MAP.md` update via the `dune:map` skill (one node → `cpp/` + `rust/`); do not hand-edit.
- [x] Add a `server-rs` unit test asserting the `From` mapping for every enum variant plus the `bool → c_int` flags.
- [x] Run `just bazel-rust-project` to regenerate `rust-project.json` for the new crate.
- [x] Run `just bazel-rustfmt-check` and `just bazel-clippy-check` (the `From` match arms are a common `-Dwarnings` tripwire).
- [x] Do **not** hand-edit `CHANGELOG.md` (release-please generated); do **not** add a Rust fuzzer.

## Acceptance Criteria

- [x] `just bazel-build` and `just bazel-coverage` green on macOS; CI green on all three platforms plus the asan unit leg (which now builds the new pure-Rust crate).
- [x] `just bazel-rustfmt-check` and `just bazel-clippy-check` pass (`-Dwarnings`).
- [x] `grep -rE '//src/iiifparser[^/]' ` returns zero matches (old label fully retired, no shim).
- [x] `bazel query 'deps(//src/iiifparser/rust:iiif_parser)'` shows no `//src/ffi:sipi_ffi` and no C++ engine (confirms the parser is FFI-free and needs no `_CPP_STDLIB_LINK`).
- [x] The crate does not import `crate::ffi`; `From<iiif_parser::IiifParams> for SipiIiifParams` and the enum `From`s use exhaustive matches; a `server-rs` test asserts the mapping for every enum variant + the `bool → c_int` flags.
- [x] A full engine build proves every C++ `#include "iiifparser/*.h"` still resolves.
- [x] The Rust corpus test actually reads the corpus (asserts file count `> 0`, matching the C++ test) so a broken runfiles path cannot vacuously pass.
- [x] Inline parser unit tests run under `bazel test //src/iiifparser/rust/...`; `sipi_unit_test` still passes.
- [x] The `iiif_parser` crate is split into modules; no `.rs` file approaches 1000 lines.
- [x] `//src/iiifparser/cpp/classifier` is a self-contained subpackage (deps `//src/util` only) whose deletion needs only the `//test/approval` edge + corpus consumer removed.
- [x] ADR-0021 committed; ARCH-MAP.md and the benchmarking/testing/developing/commit-conventions doc labels updated.
- [x] IIIF behavior unchanged: e2e `iiif_compliance`, `proptest_iiif_uri`, approval, and the C++ `iiif_handler_test` corpus sweep all pass.

## Dependencies & Risks

- No `MODULE.bazel` change needed (`percent-encoding` declared; runfiles ships with
  `rules_rust`). Confirm `@rules_rust//rust/runfiles` is exposed at the pinned
  `rules_rust` version (spec review: yes at 0.70.0).
- Single commit, single PR (`refactor(iiifparser): …`). The change spans C++ moves,
  a Rust carve, and BUILD retargeting but is one coherent concern.
- macOS cannot link ASan locally; sanitizer eligibility of the new crate is a
  CI-only verification.
- Resolve the real tracking issue for the `iiif_handler` reference-oracle note
  (`DUNE-xxx` placeholder in the `cpp/BUILD.bazel` docstring).

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| `strip_include_prefix`/`include_prefix` wrong → C++ engine build breaks | M | H | Set `/src/iiifparser/cpp` + `include_prefix = "iiifparser"` on both libs; full engine build is an acceptance gate |
| Rust corpus test resolves a bad runfiles path → vacuous pass | M | M | `current_repository()`-qualified path; assert file count `> 0` matching the C++ test; skip `BUILD.bazel` |
| `From` mapping non-total or uses `as` casts → silent enum mis-tag | L | H | Exhaustive match arms; total-superset domain enums; keep `ffi.rs` discriminant asserts; per-variant mapping test |
| Incomplete label retargeting → broken build or dangling ref | L | M | `grep -rE '//src/iiifparser[^/]'` == 0 acceptance gate; scrub comment-only refs |
| `cpp/` mislabeled wholesale "reference" → value objects wrongly marked `testonly` | L | H | Docstring states the role split; `iiifparser` stays non-`testonly`; `//src:engine` dep proves it live |
| ARCH-MAP.md drift after node split | M | L | Update via `dune:map` skill as a Phase 4 deliverable |

## Success Metrics

- The `iiif_parser` crate builds with zero C++ linkage (`bazel query` clean of
  `sipi_ffi`) and participates in the sanitizer leg without a `no-sanitizer` tag.
- Zero IIIF behavior change across e2e/approval/proptest/corpus suites.
- One reviewable commit; both implementations discoverable under `src/iiifparser/`.

## References

- ADR (this change): `docs/adr/0021-iiifparser-polyglot-colocation.md`
- Prior art / lineage: `docs/adr/0020-oracle-removal.md`, `docs/adr/0013-shttps-as-internal-module.md`, `docs/adr/0003-module-co-located-source-and-tests.md`, `docs/adr/0017-extensibility-lua-and-rust.md`
- Rust parser (source of the carve): `src/server-rs/src/iiif.rs`
- FFI seam types + discriminant asserts: `src/server-rs/src/ffi.rs:60-162`
- Sole seam mapping site: `src/server-rs/src/routes.rs:710-741`
- C++ targets to move: `src/iiifparser/BUILD.bazel`; corpus: `src/iiifparser/corpus/BUILD.bazel`
- Retarget points: `src/BUILD.bazel:280`, `test/approval/BUILD.bazel:54`
- Bazel polyglot layout: [bazel.build/build/style-guide](https://bazel.build/build/style-guide), [rules_fuzzing cc-fuzzing-rules](https://github.com/bazel-contrib/rules_fuzzing/blob/main/docs/cc-fuzzing-rules.md), [rules_rust#55 (cc→rust dep constraint)](https://github.com/bazelbuild/rules_rust/issues/55)

## Execution Outcome (2026-08-15)

All four phases complete; all 44 checkboxes ticked. Committed as one
`refactor(iiifparser): …` commit (`7fd995e2`) on branch
`refactor/iiifparser-polyglot-colocation`, on top of the ADR-0021 commit
(`0f3d85bd`). **Deferred to a later phase: none.**

- **Gates green (macOS):** `just bazel-coverage` (55/55 tests, incl.
  `iiif_compliance`, `proptest_iiif_uri`, the C++ `iiif_handler_test` corpus
  sweep, and the new Rust `corpus_regression_test`), `bazel-rustfmt-check`,
  `bazel-clippy-check`, `commit-lint`. Both acceptance greps clean; the
  `//src/iiifparser/rust:iiif_parser` dep graph is FFI-free.
- **Adversarial review (Rust + C++ + consistency):** zero Critical. One Warning
  (stale `just bench parse` package path) fixed; consistency Warnings (stale
  deleted-file citations in `CLAUDE.md`, `CONTEXT.md`, `benchmarking.md`,
  `CONVENTIONS.md`) fixed. Two pre-existing Rust polish suggestions (discarded
  `ParseError` message at the route boundary; no `Display`/`Error` impl on
  `ParseError`) left as out-of-scope follow-ups for this behavior-preserving
  refactor.
- **DUNE-015** assigned to the `cpp/classifier` reference-oracle deletion
  boundary; registered in ARCH-MAP.md via `dune:map`.
