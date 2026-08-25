---
title: "feat: Bazel-native fuzz harness for the IIIF parser (rules_fuzzing + FFI shim)"
type: feat
date: 2026-08-25
author: "Ivan Subotic"
status: reviewed
repository: dasch-swiss/sipi
linear: DEV-6970
---

# feat: Bazel-native fuzz harness for the IIIF parser (rules_fuzzing + FFI shim)

## Overview

Stand up coverage-guided libFuzzer fuzzing of the production IIIF URL parser
`iiif_parser::parse_request` (`src/iiifparser/rust/request.rs:92`), entirely
inside Bazel. The harness is a `cc_fuzz_test` from `rules_fuzzing` whose
`LLVMFuzzerTestOneInput` calls a small `extern "C"` shim over the Rust parser
(a `rust_static_library`), with the Rust crate graph instrumented for
SanitizerCoverage via **stable** rustc flags — no nightly toolchain, no Cargo,
no parallel build system. Seeded from the existing 241-file shared corpus
(`//src/iiifparser/corpus:seed_corpus`), wrapped in `just` recipes, and run
nightly in CI on linux-x86_64.

This is the tracked follow-up from Phase 7 of the agent-legibility remediation
(DEV-6968 / plan `01`): the retired C++ harness fuzzed the oracle-only
classifier; since ADR-0020 there has been no coverage-guided fuzzing of the
parser that production actually runs.

**Maintainer decision (2026-08-25):** stay complete inside Bazel using
`rules_fuzzing` plus a small FFI layer — superseding both the DEV-6970 issue
text ("wire into Bazel via rules_rust" alone) and ADR-0021's sketch of a
cargo-fuzz crate outside Bazel.

## Problem Statement / Motivation

- `parse_request` is the first parser every HTTP request hits: it classifies
  the URI (`Iiif | InfoJson | KnoraJson | Redirect | FileDownload`) and parses
  `{region}/{size}/{rotation}/{quality}.{format}` from attacker-controlled
  input. A panic here is a remote-triggerable request failure (and, under
  load, an availability lever).
- Since Phase 7 of DEV-6968 deleted `//fuzz/handlers`, the only corpus
  exercise is the fixed 241-file regression sweep
  (`//src/iiifparser/rust:corpus_regression_test`) — no mutation, no coverage
  guidance. `docs/src/development/testing-strategy.md` explicitly lists
  "extremely long URL" inputs as a gap only fuzzing closes.
- The crate is an ideal fuzz target: FFI-free, one dependency
  (`percent-encoding`), pure CPU, already sanitizer-eligible.

## Proposed Solution

New Bazel package `src/iiifparser/fuzz/` (a sibling subpackage of the
colocated polyglot module, consistent with `ARCH-MAP.md`'s `iiifparser`
component):

```
src/iiifparser/fuzz/
├── BUILD.bazel
├── shim.rs          # rust_static_library: extern "C" over parse_request
└── fuzz_target.cc   # LLVMFuzzerTestOneInput → shim
```

**Shim (`shim.rs`)** — the first C++-calls-Rust seam in the repo (all existing
FFI is Rust→C++). Hand-written declaration on the C++ side, mirroring the
hand-written `sipi_ffi.h` convention; no cbindgen.

```rust
#![warn(clippy::undocumented_unsafe_blocks)]

use std::slice;

/// Parse one fuzzer-supplied input as an IIIF request URI.
///
/// Invalid UTF-8 is rejected, not lossy-converted: every production caller
/// (axum path extraction) hands the parser a valid `&str`, so U+FFFD inputs
/// are structurally unreachable and would only produce untriageable findings.
///
/// # Safety
///
/// `data` must point to `len` initialized bytes, per libFuzzer's
/// `LLVMFuzzerTestOneInput` contract.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn sipi_fuzz_parse_request(data: *const u8, len: usize) -> i32 {
    // SAFETY: libFuzzer guarantees `data`/`len` describe a live, initialized
    // buffer for the duration of the call.
    let bytes = unsafe { slice::from_raw_parts(data, len) };
    if let Ok(uri) = std::str::from_utf8(bytes) {
        let _ = iiif_parser::parse_request(uri);
    }
    0
}
```

`unsafe extern "C"` plus the `# Safety` section and `// SAFETY:` comment are
required, not stylistic: `#[unsafe(no_mangle)]` under edition 2021 wants the fn
marked `unsafe`, and the crate root enables `clippy::undocumented_unsafe_blocks`
(the repo convention for every crate containing `unsafe`), which CI's
`-Dwarnings` promotes to an error.

```cpp
// fuzz_target.cc
#include <cstddef>
#include <cstdint>

extern "C" int sipi_fuzz_parse_request(const uint8_t *data, size_t len);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  return sipi_fuzz_parse_request(data, size);
}
```

```python
# src/iiifparser/fuzz/BUILD.bazel
load("@rules_fuzzing//fuzzing:cc_defs.bzl", "cc_fuzz_test")
load("@rules_rust//rust:defs.bzl", "rust_static_library")

rust_static_library(
    name = "parse_request_shim",
    srcs = ["shim.rs"],
    edition = "2021",
    deps = ["//src/iiifparser/rust:iiif_parser"],
)

cc_fuzz_test(
    name = "parse_request_fuzz",
    srcs = ["fuzz_target.cc"],
    corpus = ["//src/iiifparser/corpus:seed_corpus"],
    deps = [":parse_request_shim"],
)
```

Neither target is `testonly`: `cc_fuzz_test` generates a non-`testonly`
`cc_binary` for the fuzzing loop, which a `testonly` dep fails analysis on. The
seam's confinement is that nothing outside the package depends on the shim.

**Coverage instrumentation of the Rust side** — applied config-wide in the
fuzz `.bazelrc` config via `rules_rust`'s `extra_rustc_flag` build setting
(target config only; exec-config proc-macros/build scripts unaffected). All
flags are stable rustc:

```
# .bazelrc
build:fuzz --@rules_fuzzing//fuzzing:cc_engine=@rules_fuzzing//fuzzing/engines:libfuzzer
build:fuzz --@rules_fuzzing//fuzzing:cc_engine_instrumentation=libfuzzer
build:fuzz --@llvm//config:fuzzer=true
build:fuzz --@llvm//config:ubsan=true
build:fuzz --@rules_rust//rust/settings:extra_rustc_flag=-Cpasses=sancov-module
build:fuzz --@rules_rust//rust/settings:extra_rustc_flag=-Cllvm-args=-sanitizer-coverage-level=4
build:fuzz --@rules_rust//rust/settings:extra_rustc_flag=-Cllvm-args=-sanitizer-coverage-inline-8bit-counters
build:fuzz --@rules_rust//rust/settings:extra_rustc_flag=-Cllvm-args=-sanitizer-coverage-pc-table
build:fuzz --@rules_rust//rust/settings:extra_rustc_flag=-Cllvm-args=-sanitizer-coverage-trace-compares
build:fuzz --@rules_rust//rust/settings:extra_rustc_flag=-Cpanic=abort
```

The **singular** `extra_rustc_flag` is required: it accumulates across
occurrences, whereas each plural `extra_rustc_flags` occurrence replaces the
whole list, so a plural block silently keeps only its last line.

`-Cpanic=abort` is set explicitly for the whole fuzz-config crate graph rather
than relying on the (correct since Rust 1.71, but implicit)
abort-on-unwind-across-`extern "C"` default — smaller binary, unambiguous
crash semantics for libFuzzer.

**ASan pairing** is `--config=fuzz --config=asan` — the repo's own sanitizer
config, not rules_fuzzing's `cc_engine_sanitizer` (which injects
`-fsanitize=…` without provisioning a runtime and so must stay at `none`). The
two compose because the rules_fuzzing transition re-emits `--copt`/`--linkopt`
while `--@llvm//config:asan=true` passes through it untouched. The Rust parser
is safe code and already runs under the ASan/UBSan CI leg via its unit and
corpus tests; the fuzz-specific ASan value is at the shim boundary
(`*const u8, usize` → `&[u8]` → `&str`), in libFuzzer's own buffer handling,
and in the linked native runtime — ASan does **not** instrument the Rust crate
graph (that needs nightly `-Zsanitizer`, which this design avoids).

**Corpus policy — two tiers, artifact-chained (same workflow the retired
`fuzz.yml` used):**

1. **Checked-in seed corpus** — `//src/iiifparser/corpus:seed_corpus`
   (241 files, shared with the C++ classifier test and
   `corpus_regression_test`). Grows only by deliberate, human-committed
   merges (below) and by crash reproducers committed with the fix for the
   bug they reproduce — never silently (the filegroup also feeds the C++ and
   Rust corpus regression sweeps).
2. **Live working corpus** — chained between nightly runs as a GitHub
   Actions **artifact** (`fuzz-corpus`), not the cache (artifacts chain
   explicitly across runs, are downloadable for the manual merge step, and
   aren't subject to cache eviction). Each run: locate the last successful
   `fuzz.yml` run via `gh api`, download its `fuzz-corpus` artifact, seed
   the working dir from checked-in seeds + artifact (fallback: seeds alone
   on first run or expired retention), fuzz, then minimize with libFuzzer
   `-merge=1` and upload the merged result as this run's artifact.
3. **Periodic pull-into-repo** — `just fuzz-corpus-merge` downloads the
   latest `fuzz-corpus` artifact and `-merge=1`s it into
   `src/iiifparser/corpus/`, importing only coverage-adding inputs; the
   maintainer reviews the diff and commits. This is the only path from live
   corpus to checked-in corpus.

## Alternative Approaches Considered

1. **cargo-fuzz crate outside Bazel** (ADR-0021's sketch; the rust-fuzz /
   OSS-Fuzz standard). Rejected by maintainer: introduces the repo's first
   `Cargo.toml`, a rustup-managed nightly toolchain outside the hermetic
   build, and a fuzz crate invisible to the `bazel-rustfmt`/`clippy` gates —
   breaks the "Bazel is the build system" invariant and the flake.nix
   decision to keep a parallel cargo path out of the dev shell.
2. **Pure-Rust libFuzzer via rules_rust + libfuzzer-sys.** Requires
   `-Zsanitizer` (nightly-only); MODULE.bazel registers only stable 1.89.0,
   and per-target nightly selection needs custom channel-transition plumbing
   with no upstream support and no repo prior art. Disproportionate.
3. **Corpus regression only (status quo).** Already shipped
   (`corpus_regression_test`); provides no mutation or coverage guidance and
   leaves the testing-strategy fuzz row unfilled. Kept as the per-PR net, not
   a substitute.

## Technical Considerations

- **First reverse-direction FFI seam.** Everything under `src/ffi/` +
  `src/server-rs/src/ffi.rs` is Rust-calls-C++; this adds the first
  C++-calls-Rust link. It is confined to the fuzz package (nothing outside it
  depends on the shim, so it never reaches the production binary), with a
  hand-written `extern "C"` declaration mirroring the `sipi_ffi.h` convention. `rust_static_library` provides `CcInfo`
  (verified in rules_rust 0.70.0 source), so `cc_fuzz_test.deps` consumes the
  shim directly — no `cc_library` wrapper needed.
- **libFuzzer runtime under hermetic LLVM.** The `llvm` module (0.8.10,
  LLVM 22.1.7) lists `fuzzer` in its `SANITIZERS`, so
  `--@llvm//config:fuzzer=true` both adds `-fsanitize=fuzzer` to compile+link
  and stages `clang_rt.fuzzer*` — compiler-rt built from source — into the
  toolchain resource directory. Same machinery `--config=asan` uses (DEV-6563);
  ADR-0014's "no libFuzzer runtime" note is stale for 0.8.10. `config:ubsan` is
  required alongside it: the clang driver links `libclang_rt.ubsan_standalone`
  for `-fsanitize=fuzzer`, and only `config:ubsan=true` stages that archive.
- **macOS is not supported for `--config=fuzz`.** `@llvm//toolchain:resource_dir`
  selects `[]` for `@platforms//os:macos`, so `-resource-dir` is never passed and
  the driver looks for sanitizer runtimes in the prebuilt toolchain's empty
  `lib/darwin/`. Same root cause as the documented "macOS cannot link ASan
  locally". The first-party-`cc_library`-libFuzzer fallback is closed too:
  compiler-rt's libFuzzer sources do not compile for darwin under this
  toolchain's runtime-bootstrap config (`FuzzerDefs.h: 'cassert' file not
  found`). macOS keeps the default replay engine — the target builds and
  corpus-replays there, so the build-completeness invariant holds; the mutation
  loop and its `just` recipes are Linux-gated, like the sanitizer legs.
- **rules_fuzzing 0.8.0 (BCR) under bzlmod / Bazel 9.** C++/Java only, which
  is fine — the Rust side enters through the shim. A plain `bazel_dep`
  suffices (its module extensions are self-consumed; verified against its
  v0.8.0 `MODULE.bazel`). Known dep-graph effect to confirm in the spike: it
  requires **rules_python 1.8.0 as a non-dev dep**, so MVS bumps the repo's
  `rules_python@1.7.0 (dev_dependency)` pin to 1.8.0.
- **Default engine is `replay`.** `--@rules_fuzzing//fuzzing:cc_engine`
  defaults to `//fuzzing/engines:replay` with instrumentation `none`, so
  without `--config=fuzz` the target builds with no libFuzzer runtime and
  `bazel test //src/iiifparser/fuzz:parse_request_fuzz` is a corpus-replay
  regression run — the build-completeness invariant (every target builds on
  every platform, macOS included) holds by default, and only `--config=fuzz`
  needs the runtime. Because `just bazel-test`, `bazel-test-unit`, and
  `bazel-test-sanitized` all sweep `//src/...` (`justfile:74,120,216`), the
  replay test joins the per-PR matrix on all platforms and the sanitizer leg
  **automatically — this is intended**: an ASan/UBSan'd corpus replay through
  the shim on every PR, complementary to `corpus_regression_test` (which
  sweeps the crate API directly, without the shim/engine path).
  `cc_fuzz_test` also generates a `parse_request_fuzz_run` launcher for
  `bazel run`; CI's nightly loop still executes the built binary directly to
  control corpus growth outside the sandbox, at
  `bazel-bin/src/iiifparser/fuzz/parse_request_fuzz_bin` — a stable symlink
  declared by `fuzzing_binary` into the transitioned config's config-hashed
  `…_raw_` path, which must never be hardcoded. Every generated target except
  the test itself is `manual`-tagged upstream, so `_bin` is built by naming it
  (or the test target) explicitly, not by a `//src/...` wildcard.
- **Stable-rustc sancov.** `-Cpasses=sancov-module` + `-Cllvm-args=…` are
  stable flags; instrumentation is emitted by rustc's own LLVM, so the only
  cross-LLVM contact point is the runtime symbols resolved at link. libFuzzer
  observes the Rust edges: 1119 inline 8-bit counters vs 8 for a shim-only
  build, `cov: 628` vs `cov: 1` at INITED, and a recommended dictionary of
  string literals (`bitonal`, `default`, `gif`) that exist only in the Rust
  crate.
- **RBE vs runner execution.** Building the instrumented binary is a normal
  cacheable RBE action (the sanitizer CI job already builds via RBE). The
  long-lived fuzz *loop* is not an RBE action: CI builds via
  `just bazel-build-fuzz`, then executes the built binary directly on the
  runner with runner-local corpus/artifact directories — not `bazel run`,
  whose sandbox would silently discard corpus growth.
- **Panic behavior.** Rust panic → abort (explicit `-Cpanic=abort`) → SIGABRT
  → libFuzzer crash report with the input saved as `crash-<sha1>`, process exit
  code 77 — so CI's fail-loudly step needs no crash detection of its own.
  Verified with a temporary injected panic, since no known panic exists in
  `parse.rs`/`request.rs` to use as a natural canary.
- **Input edge cases.** Empty input and interior NULs need no shim handling
  (`tokenize()` handles empty; NUL is a valid UTF-8 scalar the parser treats
  as any other byte) — seed one embedded-`\0` corpus entry to lock that in.
  No `arbitrary` crate: a single `&str` parameter does not justify the
  dependency.
- **Commit slicing** (one PR, self-contained commits, scope `iiifparser` —
  already covers the whole `src/iiifparser/` tree, no new scope):
  `test(iiifparser)` for the harness + Bazel/justfile wiring (kept as one
  self-contained commit — the `.bazelrc`/MODULE.bazel wiring is inert without
  the package), `chore(ci)` for the nightly workflow, `docs(docs)` for the doc
  repairs.

## Implementation Phases

#### Phase 1: Spike — feasibility gates (all six pass; the verified configuration is what this plan now documents)

- [x] Add `bazel_dep(name = "rules_fuzzing", version = "0.8.0")` and confirm the module graph resolves under Bazel 9 bzlmod — MVS bumps `rules_python` 1.7.0 → 1.8.0 (rules_fuzzing requires it non-dev), so the repo's dev-dependency declaration is bumped to 1.8.0 to match; no `single_version_override` needed
- [x] Confirm a minimal `cc_fuzz_test` links and runs against the hermetic LLVM toolchain with `-fsanitize=fuzzer` (engine `@rules_fuzzing//fuzzing/engines:libfuzzer`) — the toolchain provisions the runtime behind `--@llvm//config:fuzzer=true` (plus `config:ubsan=true`) on Linux; the first-party-`cc_library` fallback is unnecessary and, on darwin, unusable
- [x] Exercise the C++→Rust link: `cc_fuzz_test.deps = [":parse_request_shim"]` (`rust_static_library` provides `CcInfo` per rules_rust 0.70.0 — confirmed upstream; the spike proves it against this repo's toolchain)
- [x] Confirm the stable sancov `extra_rustc_flag` set instruments the Rust crate graph: libFuzzer's `cov:` counters grow beyond the C++-shim baseline when fuzzing (628 vs 1, 1119 vs 8 inline counters)
- [x] Panic canary: with a temporarily injected panic in `parse_request`, confirm libFuzzer reports a crash (SIGABRT) and saves the reproducer input
- [x] ASan pairing: `--config=fuzz --config=asan` composes; rules_fuzzing's `cc_engine_sanitizer` cannot provision a runtime and stays at `none` — recorded in the `.bazelrc` comment block

#### Phase 2: Harness package + build wiring

- [x] Create `src/iiifparser/fuzz/BUILD.bazel` with `rust_static_library` shim + `cc_fuzz_test` seeded from `//src/iiifparser/corpus:seed_corpus`, package docstring explaining the reverse-FFI seam and why its confinement cannot be a `testonly` marker
- [x] Implement `shim.rs` (UTF-8 reject, result discarded, doc comment stating the production-contract rationale)
- [x] Implement `fuzz_target.cc` (hand-written extern declaration, `sipi_ffi.h` convention)
- [x] Add the `build:fuzz` config block to `.bazelrc` with the sancov + `-Cpanic=abort` `extra_rustc_flag` lines, documenting the ASan pairing and the macOS limitation in the file's existing comment style
- [x] Add an embedded-NUL seed file to `src/iiifparser/corpus/` (content-sha1 filename, matching the existing seeds), and exclude `BUILD.bazel` from the `seed_corpus` glob so the filegroup is exactly the seed inputs
- [x] Verify visibility: `//src/iiifparser/fuzz` consumes `//src/iiifparser/rust:iiif_parser` within the existing `//src:__subpackages__` grant (no new visibility edges)
- [x] Update `ARCH-MAP.md`'s `iiifparser` component entry with the new `fuzz/` subpackage (and the reverse-FFI boundary rule); freshness is date-tracked, not SHA-tracked, per the map's own header

#### Phase 3: justfile integration

- [x] Add `just bazel-build-fuzz` (builds `//src/iiifparser/fuzz:parse_request_fuzz_bin` with `--config=fuzz`; RBE-eligible, CI-invoked — `bazel-*` naming). `_bin`, not the test target: it is what the loop executes, only a top-level target materialises under the BwoB default, and the test target cannot be configured for a non-host platform (no test toolchain). Linux-gated with a clear error on darwin
- [x] Add `just fuzz *FLAGS` (local/dev-loop bare-name recipe: builds, then executes the binary directly with a writable working-corpus dir under `.fuzz/`, re-seeded from the checked-in corpus each run; passes `FLAGS` through to libFuzzer, e.g. `-max_total_time=60`)
- [x] Add `just fuzz-corpus-merge` (depends on `bazel-build-fuzz` — `-merge=1` needs the instrumented binary; downloads the latest `fuzz-corpus` artifact via `gh run list`/`gh api` + `gh run download`, merges from the artifact into `src/iiifparser/corpus/` so only coverage-adding inputs are imported, prints the resulting diff for review — committing stays manual)
- [x] Extend the `bazel-rustfmt-check` and `bazel-clippy-check` target lists (`justfile:139`, `justfile:148`) to cover the new package (broaden `//src/iiifparser/rust/...` to `//src/iiifparser/...`)
- [x] Re-run `just bazel-rust-project` so rust-analyzer sees the shim crate (`rust-project.json` is gitignored — regenerated per checkout, never committed)

#### Phase 4: Nightly CI workflow

- [ ] Add `.github/workflows/fuzz.yml`: scheduled nightly, linux-x86_64, builds via `just bazel-build-fuzz` through the `bazel-rbe` composite action (same shape as the `sanitizer` job in `ci.yml`), then runs the binary directly on the runner with a bounded budget (`-max_total_time`, start at 600s)
- [ ] Restore the working corpus at run start: `gh api` locates the last successful `fuzz.yml` run, `gh run download` fetches its `fuzz-corpus` artifact; fall back to the checked-in seeds alone when none exists (first run / expired retention)
- [ ] After the fuzz loop, minimize the working corpus with libFuzzer `-merge=1` and upload it as this run's `fuzz-corpus` artifact (retention: 90 days) — the next run chains from it; the checked-in seed filegroup is never written by CI
- [ ] On crash: workflow fails loudly (non-green nightly status), uploads crash reproducers + libFuzzer log as artifacts with `retention-days: 30` (mirroring the sanitizer job); triage is manual — no auto-filed Linear issue (explicit decision, revisit if nightly findings become frequent)
- [ ] Attach the BEP JSON artifact for the build step like the other CI legs (`bep-*` convention); the runner-local fuzz loop is not a Bazel invocation and produces no BEP

#### Phase 5: Documentation truth pass

- [ ] Rewrite `docs/src/development/fuzzing.md`: current harness (target, shim seam, configs, just recipes, corpus policy, nightly cadence), plus the explicit rationale that `corpus_regression_test` is the per-PR net and up-to-24h fuzz latency on new regressions is accepted
- [ ] Fix `docs/adr/0020-oracle-removal.md:59`: the fuzz follow-up issue is DEV-6970, not DEV-6969
- [ ] Fix dangling fuzz references: `docs/src/development/bazel.md:196,198`, `docs/src/development/ci.md:50,105` (and the follow-up note at `ci.md:255-256`), `.github/actions/ci-setup/action.yml:5`
- [ ] Update `docs/src/development/testing-strategy.md`: fill the layer-4 fuzz row (target, status, invocation), close the "extremely long URL" gap marker, align the CI-distribution line ("fuzz (nightly)") with the shipped workflow
- [ ] Note the reverse-FFI seam in `CONVENTIONS.md` § Module Layout (or the FFI section) so the next agent doesn't treat C++→Rust linking as unprecedented

## Acceptance Criteria

- [x] `just bazel-build-fuzz` builds the instrumented fuzz binary on Linux; the recipe is linux-gated like the sanitizer legs (macOS cannot link the libFuzzer runtime — see Technical Considerations). Verified by cross-building `//src/iiifparser/fuzz:parse_request_fuzz_bin` under `--config=fuzz --platforms=//platforms:linux_aarch64`, which yields a Linux aarch64 ELF
- [x] `just fuzz -max_total_time=60` runs the mutation loop on Linux: every seed replays clean, libFuzzer's coverage counters exceed the shim-only baseline (Rust instrumentation proven). macOS gets `bazel test //src/iiifparser/fuzz:parse_request_fuzz` (corpus replay) instead
- [x] Panic-canary result recorded: a parser panic surfaces as a libFuzzer crash (SIGABRT, exit 77) with a saved `crash-<sha1>` reproducer
- [ ] Nightly `fuzz.yml` runs green on schedule; a second run demonstrably restores the previous run's `fuzz-corpus` artifact (chaining works); a forced failure demonstrates crash-artifact upload
- [ ] `just fuzz-corpus-merge` imports coverage-adding inputs from the latest CI artifact into `src/iiifparser/corpus/` and leaves the commit to the maintainer
- [x] `just bazel-rustfmt-check` and `just bazel-clippy-check` cover `//src/iiifparser/fuzz` and pass
- [ ] Existing gates green with the new target included: `//src/...` test sweeps (`bazel-test`, `bazel-test-unit`, `bazel-test-sanitized`) now pick up the replay-mode fuzz test on all platforms — intended; unit, approval goldens, e2e all green; production binary contents unchanged (fuzz package is test-only)
- [ ] All Phase 5 doc fixes landed; `fuzzing.md` describes only what exists
- [ ] PR closes DEV-6970

## Dependencies & Risks

- **rules_fuzzing 0.8.0** (new `bazel_dep`) — C++/Java-only rule set used
  exactly for its C++ path; Rust enters via the shim.
- **Hermetic LLVM fuzzer runtime** — provisioned by the toolchain behind
  `--@llvm//config:fuzzer=true` on Linux; no toolchain change and no custom
  engine needed. Not available on darwin, which is why the fuzz recipes are
  linux-gated.
- **rules_rust 0.70.0 `@rules_rust//rust/settings:extra_rustc_flags`** —
  verified: target-config only (a separate `extra_exec_rustc_flags` exists
  for exec-config, deliberately untouched here).
- No production-code changes; the blast radius is build graph + CI + docs.

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Hermetic LLVM lacks `-fsanitize=fuzzer` runtime | — | — | Resolved: the toolchain builds compiler-rt's libFuzzer from source behind `--@llvm//config:fuzzer=true` (+ `config:ubsan=true`) on Linux |
| `rust_static_library`→`cc_fuzz_test` link fails against this toolchain | — | — | Resolved: links directly against this repo's toolchain; no `cc_library` wrapper needed |
| rules_python MVS bump 1.7.0→1.8.0 breaks something | — | — | Resolved: the declaration is bumped to 1.8.0 to match MVS; every gate green, no `single_version_override` |
| Stable sancov flags don't yield usable coverage on rustc 1.89 | — | — | Resolved: 1119 inline counters vs 8 shim-only, `cov: 628` vs `1`, and `trace-compares` recovers Rust-only string literals |
| Fuzzer-found inputs polluting the shared regression corpus | M | M | Explicit corpus policy: runner-side working corpus, check-in only deliberate reproducers with fixes |
| Nightly-only cadence delays detection up to 24h | certain | L | Accepted; `corpus_regression_test` is the per-PR net; documented in fuzzing.md |

## Success Metrics

- Coverage-guided fuzzing of the production parser exists again (gap open
  since ADR-0020) and runs unattended nightly.
- Any parser panic reachable from a request URI is discoverable as a saved,
  replayable reproducer instead of a production 500.
- The testing-strategy fuzz row is filled truthfully; zero dangling fuzz
  references remain in the docs.

## References

- Fuzz target: `src/iiifparser/rust/request.rs:92` (`parse_request`), crate `//src/iiifparser/rust:iiif_parser` (`src/iiifparser/rust/BUILD.bazel:21-33`)
- Seed corpus: `src/iiifparser/corpus/BUILD.bazel:19-22` (`:seed_corpus`, 241 files); Rust regression sweep `src/iiifparser/rust/corpus_regression_test.rs`
- Toolchain facts: `MODULE.bazel:478-504` (rules_rust 0.70.0, stable 1.89.0 only); `.bazelrc:189-249` (ASan/UBSan via `--@llvm//config:asan=true`, compiler-rt from source)
- CI precedent for artifacts/RBE: `sanitizer` job in `.github/workflows/ci.yml`
- Decision history: `docs/adr/0020-oracle-removal.md` (harness retirement, follow-up), `docs/adr/0021-iiifparser-polyglot-colocation.md:138-142` (superseded outside-Bazel sketch), `docs/adr/0014-toolchain-provider-swap.md` (libFuzzer runtime history), parent plan `01-refactor-sipi-agent-legibility-plan.md` (Phase 7)
- rules_fuzzing: https://github.com/bazel-contrib/rules_fuzzing (BCR 0.8.0; C++/Java engines, custom `cc_fuzzing_engine` support)
- Stable-rustc SanitizerCoverage flags: cargo-fuzz's sancov flag set (`-Cpasses=sancov-module`, `-Cllvm-args=-sanitizer-coverage-*`); nightly is required only for `-Zsanitizer`, which this design avoids
