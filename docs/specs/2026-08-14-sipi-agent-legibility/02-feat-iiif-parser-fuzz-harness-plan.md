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
use std::slice;

/// Fuzz entry point over `iiif_parser::parse_request`.
///
/// Invalid UTF-8 is rejected, not lossy-converted: every production caller
/// (axum path extraction) hands the parser a valid `&str`, so U+FFFD inputs
/// are structurally unreachable and would only produce untriageable findings.
#[unsafe(no_mangle)]
pub extern "C" fn sipi_fuzz_parse_request(data: *const u8, len: usize) -> i32 {
    let bytes = unsafe { slice::from_raw_parts(data, len) };
    if let Ok(uri) = std::str::from_utf8(bytes) {
        let _ = iiif_parser::parse_request(uri);
    }
    0
}
```

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
# src/iiifparser/fuzz/BUILD.bazel (sketch — exact deps shape settled by the spike)
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

**Coverage instrumentation of the Rust side** — applied config-wide in the
fuzz `.bazelrc` config via `rules_rust`'s `extra_rustc_flags` build setting
(target config only; exec-config proc-macros/build scripts unaffected). All
flags are stable rustc:

```
# .bazelrc (sketch)
build:fuzz --@rules_fuzzing//fuzzing:cc_engine=@rules_fuzzing//fuzzing/engines:libfuzzer
build:fuzz --@rules_fuzzing//fuzzing:cc_engine_instrumentation=libfuzzer
build:fuzz --@rules_rust//rust/settings:extra_rustc_flags=-Cpasses=sancov-module
build:fuzz --@rules_rust//rust/settings:extra_rustc_flags=-Cllvm-args=-sanitizer-coverage-level=4
build:fuzz --@rules_rust//rust/settings:extra_rustc_flags=-Cllvm-args=-sanitizer-coverage-inline-8bit-counters
build:fuzz --@rules_rust//rust/settings:extra_rustc_flags=-Cllvm-args=-sanitizer-coverage-pc-table
build:fuzz --@rules_rust//rust/settings:extra_rustc_flags=-Cllvm-args=-sanitizer-coverage-trace-compares
build:fuzz --@rules_rust//rust/settings:extra_rustc_flags=-Cpanic=abort
```

`-Cpanic=abort` is set explicitly for the whole fuzz-config crate graph rather
than relying on the (correct since Rust 1.71, but implicit)
abort-on-unwind-across-`extern "C"` default — smaller binary, unambiguous
crash semantics for libFuzzer.

**ASan pairing:** one CI variant runs the ASan-paired engine config. The Rust
parser is safe code and already runs under the ASan/UBSan CI leg via its unit
and corpus tests; the fuzz-specific ASan value is at the shim boundary
(`*const u8, usize` → `&[u8]` → `&str`) and in libFuzzer's own buffer
handling. Whether the pairing composes as rules_fuzzing's
`cc_engine_sanitizer=asan` or the repo's existing `--config=asan`
(`--@llvm//config:asan=true`, compiler-rt from source) is a spike outcome.

**Corpus policy — two tiers, artifact-chained (same workflow the retired
`fuzz.yml` used):**

1. **Checked-in seed corpus** — `//src/iiifparser/corpus:seed_corpus`
   (241 files, shared with the C++ classifier test and
   `corpus_regression_test`). Grows only by deliberate, human-committed
   merges (below) and by crash reproducers committed with the fix for the
   bug they reproduce — never silently (the filegroup feeds two unrelated
   regression tests).
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
  C++-calls-Rust link. It is confined to a test-only package (never in the
  production binary), with a hand-written `extern "C"` declaration mirroring
  the `sipi_ffi.h` convention. `rust_static_library` provides `CcInfo`
  (verified in rules_rust 0.70.0 source), so `cc_fuzz_test.deps` consumes the
  shim directly; the spike exercises the actual link against this repo's
  toolchain.
- **libFuzzer runtime under hermetic LLVM.** ADR-0014 recorded "no libFuzzer
  runtime" under hermetic-llvm 0.8.8; the ASan/UBSan re-arm (DEV-6563) later
  added compiler-rt-from-source via `--@llvm//config:asan=true`. Whether the
  `llvm` module (0.8.10, LLVM 22.1.7) exposes `-fsanitize=fuzzer` link support
  is the central spike question. **Fallback (hermetic, no toolchain change):**
  compile LLVM's libFuzzer sources as a first-party `cc_library` and register
  it as a custom `cc_fuzzing_engine` — rules_fuzzing supports user-provided
  engines.
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
  control corpus growth outside the sandbox (the raw binary's output path is
  a macro internal — settled in the spike).
- **Stable-rustc sancov.** `-Cpasses=sancov-module` + `-Cllvm-args=…` are
  stable flags; instrumentation is emitted by rustc's own LLVM, so the only
  cross-LLVM contact point is the runtime symbols resolved at link. The spike
  verifies libFuzzer actually observes Rust edge coverage (cov counter growth
  in its log), not just that the build succeeds.
- **RBE vs runner execution.** Building the instrumented binary is a normal
  cacheable RBE action (the sanitizer CI job already builds via RBE). The
  long-lived fuzz *loop* is not an RBE action: CI builds via
  `just bazel-build-fuzz`, then executes the built binary directly on the
  runner with runner-local corpus/artifact directories — not `bazel run`,
  whose sandbox would silently discard corpus growth.
- **Panic behavior.** Rust panic → abort (explicit `-Cpanic=abort`) → SIGABRT
  → libFuzzer crash report with the input saved. Verified in the spike with a
  deliberate-panic canary (temporary `debug_assert`/injected panic), since no
  known panic exists in `parse.rs`/`request.rs` to use as a natural canary.
- **Input edge cases.** Empty input and interior NULs need no shim handling
  (`tokenize()` handles empty; NUL is a valid UTF-8 scalar the parser treats
  as any other byte) — seed one embedded-`\0` corpus entry to lock that in.
  No `arbitrary` crate: a single `&str` parameter does not justify the
  dependency.
- **Commit slicing** (one PR, self-contained commits, scope `iiifparser` —
  already covers the whole `src/iiifparser/` tree, no new scope):
  `test(iiifparser)` for the harness + Bazel/justfile wiring, `chore(ci)` for
  the nightly workflow, `docs(docs)` for the doc repairs. Confirm slicing at
  plan review if you prefer `build(iiifparser)` for the MODULE.bazel/.bazelrc
  part.

## Implementation Phases

#### Phase 1: Spike — feasibility gates (throwaway branch state, findings recorded in the PR description)

- [ ] Add `bazel_dep(name = "rules_fuzzing", version = "0.8.0")` and confirm the module graph resolves under Bazel 9 bzlmod — the known effect is an MVS bump of `rules_python` 1.7.0 → 1.8.0 (rules_fuzzing requires it non-dev); confirm the bump is harmless or add a `single_version_override` and record why
- [ ] Confirm a minimal `cc_fuzz_test` links and runs against the hermetic LLVM toolchain with `-fsanitize=fuzzer` (engine `@rules_fuzzing//fuzzing/engines:libfuzzer`); if the runtime is missing, build LLVM's libFuzzer sources as a first-party `cc_library` + custom `cc_fuzzing_engine` (rule confirmed in `@rules_fuzzing//fuzzing:cc_defs.bzl`: `library` takes any `CcInfo` target, plus a `launcher` script)
- [ ] Exercise the C++→Rust link: `cc_fuzz_test.deps = [":parse_request_shim"]` (`rust_static_library` provides `CcInfo` per rules_rust 0.70.0 — confirmed upstream; the spike proves it against this repo's toolchain)
- [ ] Confirm the stable sancov `extra_rustc_flags` set instruments the Rust crate graph: libFuzzer's `cov:` counters grow beyond the C++-shim baseline when fuzzing
- [ ] Panic canary: with a temporarily injected panic in `parse_request`, confirm libFuzzer reports a crash (SIGABRT) and saves the reproducer input
- [ ] ASan pairing: determine the working flag composition (rules_fuzzing `cc_engine_sanitizer=asan` vs `--config=asan`'s `--@llvm//config:asan=true`) and record it in the `.bazelrc` comment block

#### Phase 2: Harness package + build wiring

- [ ] Create `src/iiifparser/fuzz/BUILD.bazel` with `rust_static_library` shim + `cc_fuzz_test` seeded from `//src/iiifparser/corpus:seed_corpus`, package docstring explaining the reverse-FFI seam and its test-only confinement
- [ ] Implement `shim.rs` (UTF-8 reject, result discarded, doc comment stating the production-contract rationale)
- [ ] Implement `fuzz_target.cc` (hand-written extern declaration, `sipi_ffi.h` convention)
- [ ] Add the `build:fuzz` (and ASan-paired variant) config block to `.bazelrc` with the sancov + `-Cpanic=abort` `extra_rustc_flags`, documented in the file's existing comment style
- [ ] Add an embedded-NUL seed file to `src/iiifparser/corpus/`
- [ ] Verify visibility: `//src/iiifparser/fuzz` consumes `//src/iiifparser/rust:iiif_parser` within the existing `//src:__subpackages__` grant (no new visibility edges)
- [ ] Update `ARCH-MAP.md`'s `iiifparser` component entry with the new `fuzz/` subpackage and bump `last_verified_commit`

#### Phase 3: justfile integration

- [ ] Add `just bazel-build-fuzz` (builds `//src/iiifparser/fuzz:parse_request_fuzz` with `--config=fuzz`; RBE-eligible, CI-invoked — `bazel-*` naming)
- [ ] Add `just fuzz *FLAGS` (local/dev-loop bare-name recipe: builds, then executes the binary directly with a writable working-corpus dir seeded from the checked-in corpus; passes `FLAGS` through to libFuzzer, e.g. `-max_total_time=60`)
- [ ] Add `just fuzz-corpus-merge` (depends on `bazel-build-fuzz` — `-merge=1` needs the instrumented binary; downloads the latest `fuzz-corpus` artifact via `gh run list`/`gh api` + `gh run download`, merges from the artifact into `src/iiifparser/corpus/` so only coverage-adding inputs are imported, prints the resulting diff for review — committing stays manual)
- [ ] Extend the `bazel-rustfmt-check` and `bazel-clippy-check` target lists (`justfile:139`, `justfile:148`) to cover the new package (broaden `//src/iiifparser/rust/...` to `//src/iiifparser/...`)
- [ ] Re-run `just bazel-rust-project` so rust-analyzer sees the shim crate

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

- [ ] `just bazel-build-fuzz` builds the instrumented fuzz binary on linux-x86_64 (required); macOS-arm64 is best-effort — linking the libFuzzer runtime there is as unproven as ASan ("macOS cannot link ASan locally"), and the spike records whether it works or the recipe is linux-gated like the sanitizer legs
- [ ] `just fuzz -max_total_time=60` runs the mutation loop locally: all 241 seeds replay clean, libFuzzer's coverage counters exceed the shim-only baseline (Rust instrumentation proven)
- [ ] Panic-canary spike result recorded: a parser panic surfaces as a libFuzzer crash with a saved reproducer
- [ ] Nightly `fuzz.yml` runs green on schedule; a second run demonstrably restores the previous run's `fuzz-corpus` artifact (chaining works); a forced failure demonstrates crash-artifact upload
- [ ] `just fuzz-corpus-merge` imports coverage-adding inputs from the latest CI artifact into `src/iiifparser/corpus/` and leaves the commit to the maintainer
- [ ] `just bazel-rustfmt-check` and `just bazel-clippy-check` cover `//src/iiifparser/fuzz` and pass
- [ ] Existing gates green with the new target included: `//src/...` test sweeps (`bazel-test`, `bazel-test-unit`, `bazel-test-sanitized`) now pick up the replay-mode fuzz test on all platforms — intended; unit, approval goldens, e2e all green; production binary contents unchanged (fuzz package is test-only)
- [ ] All Phase 5 doc fixes landed; `fuzzing.md` describes only what exists
- [ ] PR closes DEV-6970

## Dependencies & Risks

- **rules_fuzzing 0.8.0** (new `bazel_dep`) — C++/Java-only rule set used
  exactly for its C++ path; Rust enters via the shim.
- **Hermetic LLVM fuzzer runtime** — the single hard feasibility gate
  (Phase 1); the custom-engine fallback keeps the plan viable without
  toolchain changes.
- **rules_rust 0.70.0 `@rules_rust//rust/settings:extra_rustc_flags`** —
  verified: target-config only (a separate `extra_exec_rustc_flags` exists
  for exec-config, deliberately untouched here).
- No production-code changes; the blast radius is build graph + CI + docs.

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Hermetic LLVM lacks `-fsanitize=fuzzer` runtime | M | H | Spike first; fallback: first-party libFuzzer `cc_library` + custom `cc_fuzzing_engine` |
| `rust_static_library`→`cc_fuzz_test` link fails against this toolchain | L | L | CcInfo provision confirmed upstream (rules_rust 0.70.0); spike exercises the actual link; thin `cc_library` wrapper as escape hatch |
| rules_python MVS bump 1.7.0→1.8.0 breaks something | L | M | Spike confirms; `single_version_override` precedent exists |
| Stable sancov flags don't yield usable coverage on rustc 1.89 | L | H | Spike gate with explicit cov-counter check; if it fails, escalate to maintainer before proceeding (would reopen the shape decision) |
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
