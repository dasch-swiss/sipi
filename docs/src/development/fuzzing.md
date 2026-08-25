# Fuzz Testing

Coverage-guided libFuzzer fuzzing of the production IIIF URL parser,
`iiif_parser::parse_request` (`src/iiifparser/rust/request.rs`). That function is
the first parser every HTTP request hits: it classifies the URI
(`Iiif | InfoJson | KnoraJson | Redirect | FileDownload`) and parses
`{region}/{size}/{rotation}/{quality}.{format}` out of attacker-controlled input,
so a panic there is a remote-triggerable request failure.

The harness lives entirely inside Bazel. The target is
`//src/iiifparser/fuzz:parse_request_fuzz`, a `cc_fuzz_test` from
`rules_fuzzing`, seeded from the shared regression corpus
`//src/iiifparser/corpus:seed_corpus`. It replaces the C++ libFuzzer harness that
fuzzed the oracle-only classifier and was retired with the oracle
([ADR-0020](../../adr/0020-oracle-removal.md)).

## The C++→Rust shim seam

libFuzzer's entry point is `LLVMFuzzerTestOneInput`, a C symbol, and
`rules_fuzzing` is a C++/Java rule set — so the Rust parser is entered through a
small `extern "C"` shim:

```
src/iiifparser/fuzz/
├── BUILD.bazel       # rust_static_library shim + cc_fuzz_test
├── shim.rs           # extern "C" sipi_fuzz_parse_request → parse_request
└── fuzz_target.cc    # LLVMFuzzerTestOneInput → shim
```

This is the repo's only **C++-calls-Rust** link — every other FFI edge
(`src/ffi/`, `src/server-rs/src/ffi.rs`) is Rust→C++. The declaration in
`fuzz_target.cc` is hand-mirrored from the Rust signature, following the
`src/ffi/sipi_ffi.h` convention (no cbindgen).

The seam never leaves the package, and Bazel enforces that rather than merely
documenting it: both targets are `testonly` (`cc_fuzz_test` forwards `testonly`
to its raw `cc_binary`, and marks every target it generates `testonly` itself),
and the package declares no default visibility. Nothing that ships can even name
the shim, let alone depend on it. Naming a target on the command line needs no
visibility, so `just fuzz` and the nightly still build `:parse_request_fuzz_bin`
directly.

The shim rejects invalid UTF-8 rather than lossy-converting it: every production
caller (axum path extraction) hands the parser a valid `&str`, so a U+FFFD input
would only yield findings unreachable from a real request. The parse result is
discarded — `Err` is a normal outcome for a malformed URI; the contract under
test is that no input panics. This is also what a corpus file *means* here: a
seed is fed to the parser verbatim, and a non-UTF-8 seed is a no-op. Note that
`corpus_regression_test` sweeps the same files with a **lossy** decode, so it can
exercise a byte sequence this harness skips.

## Two modes, one target

`rules_fuzzing`'s default engine is `//fuzzing/engines:replay` with
instrumentation `none`, so **without** `--config=fuzz` the target needs no
libFuzzer runtime and

```bash
bazel test //src/iiifparser/fuzz:parse_request_fuzz
```

is a corpus-replay regression run: every seed through the shim, asserting no
crash. It builds and runs on every platform — macOS included — and rides along in
the `//src/...` sweeps (`just bazel-test`, `bazel-test-unit`,
`bazel-test-sanitized`, `bazel-coverage`), so every PR replays the corpus, and
the sanitizer leg replays it under ASan/UBSan.

`--config=fuzz` (`.bazelrc` §Fuzzing) switches to the real libFuzzer engine and
arms SanitizerCoverage on the Rust crate graph. That is the mutation loop.

**Coverage instrumentation of the Rust side** uses only stable rustc flags —
`-Cpasses=sancov-module` plus `-Cllvm-args=-sanitizer-coverage-*`, applied
config-wide through `rules_rust`'s singular `extra_rustc_flag` build setting
(target config only; exec-config proc-macros and build scripts are untouched).
No nightly toolchain, no `-Zsanitizer`, no Cargo. libFuzzer observes the Rust
edges: 894 inline 8-bit counters versus 8 for a shim-only build, and a
recommended dictionary of substrings of Rust-only string literals (`ault` from
`default`) that exist nowhere in the C++ shim.

The config also sets `--compilation_mode=opt`. Fuzzing throughput *is* coverage —
a nightly has a fixed wall-clock window — and there is nothing to step through in
a debugger, since a finding is a saved reproducer replayed separately. Two flags
come back on top of it:

- `-Cdebug-assertions=on` and `-Coverflow-checks=on`, which `-c opt` turns off.
  An integer overflow in a coordinate or dimension parser is exactly the bug
  class this target hunts, and it is a panic in debug and a silent wraparound in
  release — turning the checks back on is what makes the fuzzer see it.
- `std::hint::black_box` around the discarded parse result in `shim.rs`, so LLVM
  cannot prove the call dead and delete the code under test.

`-Cpanic=abort` is deliberately **not** set. It would apply config-wide and so
break `bazel test --config=fuzz` on every Rust test target (the prebuilt
sysroot's `test` crate is unwind-compiled), and it buys nothing: a Rust panic
unwinding out of an `extern "C"` fn has aborted since Rust 1.71, so the
panic→SIGABRT→libFuzzer-crash path through `sipi_fuzz_parse_request` is a
language guarantee. Verified with an injected panic: the abort arrives via
`core::panicking::panic_cannot_unwind`, exit code 77, reproducer saved.

### ASan pairing

`--config=fuzz --config=asan` composes — the repo's own sanitizer config, not
`rules_fuzzing`'s `cc_engine_sanitizer` (which injects `-fsanitize=…` without
provisioning a runtime and must stay at its `none` default). Mind the scope: ASan
instruments the C++ shim, libFuzzer itself, and the linked native runtime — **not**
the Rust crate graph (that would need nightly `-Zsanitizer`, which this design
avoids). The value of the paired pass is the shim boundary
(`*const u8, usize` → `&[u8]` → `&str`), libFuzzer's own buffer handling, and the
runtime; the Rust parser is safe code that already runs under the ASan/UBSan CI
leg via its unit, corpus-regression, and replay tests.

### Linux only

`--config=fuzz` is Linux-only, and the `just` recipes below are gated on it with
a clear error on darwin. hermetic-llvm passes `-resource-dir` only on non-macOS
(`@llvm//toolchain:resource_dir` selects `[]` for `@platforms//os:macos`), so the
from-source compiler-rt runtimes are unreachable to the clang driver on darwin —
the same root cause as the Linux-only sanitizer gate. Wrapping compiler-rt's
libFuzzer as a first-party `cc_library` is closed too: those sources do not
compile for darwin under this toolchain (`FuzzerDefs.h: 'cassert' file not
found`). macOS gets the replay test, which is the default mode and needs no
extra flags.

## Recipes

```bash
just bazel-build-fuzz              # build the instrumented binary (Linux; CI-invoked, RBE-eligible)
just fuzz -max_total_time=60       # run the mutation loop locally; FLAGS pass through to libFuzzer
just fuzz-corpus-merge             # import coverage-adding inputs from the latest nightly artifact
```

`bazel-build-fuzz` builds `//src/iiifparser/fuzz:parse_request_fuzz_bin` — the
`_bin` target, not the test target: `_bin` is what the loop executes, and only a
top-level target materialises locally under the `--remote_download_minimal`
default. Its path, `bazel-bin/src/iiifparser/fuzz/parse_request_fuzz_bin`, is a
stable symlink `fuzzing_binary` declares into the transitioned config's
config-hashed `…_raw_` path; never hardcode the latter. Every generated target
except the test itself is `manual`-tagged upstream, so `_bin` is built by naming
it explicitly, not by a `//src/...` wildcard.

`just fuzz` and the nightly both execute the built binary **directly**, not via
`bazel run` — the sandbox would silently discard corpus growth. The working
corpus lives under the gitignored `.fuzz/`, re-seeded from
`src/iiifparser/corpus/` on every run so newly committed seeds are picked up.
Crash reproducers land in `.fuzz/artifacts/`.

`cc_fuzz_test` also generates a `parse_request_fuzz_run` launcher for
`bazel run`; it is not what CI or the recipes use, for the sandbox reason above.

## Corpus policy — two tiers plus the merge path between them

1. **Checked-in seed corpus** — `src/iiifparser/corpus/` (241 files, exposed as
   `//src/iiifparser/corpus:seed_corpus`). Shared with the C++ classifier test
   and `//src/iiifparser/rust:corpus_regression_test`, which is why it grows only
   deliberately: a human-committed merge (below), or a crash reproducer committed
   alongside the fix for the bug it reproduces. CI never writes it.
2. **Live working corpus** — chained between nightly runs as a GitHub Actions
   **artifact** named `fuzz-corpus`, not a cache. Artifacts chain explicitly
   across runs, are downloadable for the manual merge step, and are not subject
   to cache eviction. Each nightly seeds `.fuzz/corpus` from the checked-in seeds
   plus the artifact of the last successful run (falling back to seeds alone on
   the first run or after the 90-day retention expires), fuzzes, minimizes with
   libFuzzer `-merge=1`, and uploads the result as this run's artifact.
3. **Periodic pull-into-repo** — `just fuzz-corpus-merge` downloads the latest
   `fuzz-corpus` artifact and `-merge=1`s it into `src/iiifparser/corpus/`, so
   only coverage-adding inputs are imported, then prints the diff. Reviewing and
   committing is manual. This is the only path from the live corpus to the
   checked-in one.

`-merge=1 <dst> <src>` is libFuzzer's own minimization — it copies an input into
the destination only if it adds coverage the destination lacks, which is why it
needs the instrumented binary.

## Nightly CI

`.github/workflows/fuzz.yml`, scheduled `17 3 * * *` (03:17 UTC — off-peak for
the team and off the congested top of the hour), plus `workflow_dispatch` for
manual runs. One job on `ubuntu-24.04`, four phases: restore the working corpus,
build via `just bazel-build-fuzz` and fuzz for 600s, minimize and upload the
`fuzz-corpus` artifact, then a 300s ASan-paired pass over the minimized corpus.

The minimize and upload steps run on `!cancelled()`, and the ASan pass comes
after them, so a finding anywhere still chains the night's corpus growth forward
— losing a night of coverage to a crash would be a second injury. This is safe
because libFuzzer writes reproducers to `-artifact_prefix` and never into the
corpus directory, so a crashing input cannot enter the chain via the merge. The
ASan pass's own corpus additions are deliberately discarded — it exists to
exercise the shim/runtime boundary, not to grow coverage.

`-timeout=25` is passed to both loops and to the merge: libFuzzer's 1200s
per-input default exceeds the whole budget, so a hang would silently consume the
run instead of being reported. The merge also gets `-rss_limit_mb=4096`, since
`-merge=1` holds the whole feature set in memory as the corpus grows.

Both loops run with `RUST_BACKTRACE=1` and the hermetic `llvm-symbolizer`
(resolved to `.fuzz/llvm-symbolizer` by `just bazel-build-fuzz`) wired into the
sanitizer runtime that prints the crash trace — `UBSAN_OPTIONS=external_symbolizer_path`
for the plain loop, `ASAN_SYMBOLIZER_PATH` for the ASan pass. Without it,
first-party frames print as bare `binary+0xOFFSET`.

**Crash semantics.** A Rust panic aborts at the `extern "C"` boundary → SIGABRT →
libFuzzer writes the input as `crash-<sha1>` under `-artifact_prefix` and exits
**77**. That exit code fails the step, so the workflow needs no crash-detection
logic of its own — only preservation: the reproducers and the libFuzzer logs are
uploaded as a `fuzz-crashes` artifact (30-day retention) on failure. Triage is
manual; no issue is auto-filed. Reproduce a finding locally with

```bash
just bazel-build-fuzz
./bazel-bin/src/iiifparser/fuzz/parse_request_fuzz_bin path/to/crash-<sha1>
```

and commit the reproducer into `src/iiifparser/corpus/` together with the fix.

**Why nightly is enough.** The per-PR regression net is
`//src/iiifparser/rust:corpus_regression_test` (the crate API swept directly over
the whole corpus) plus the replay-mode `bazel test` of the fuzz target itself
(the same corpus through the shim and the engine path, on every platform, and
under ASan/UBSan on the sanitizer leg). A newly introduced panic that the
existing corpus already reaches fails the PR. Only a panic that requires
*mutation* to reach waits for the next nightly, and up to 24h of latency on that
class of finding is accepted deliberately — the alternative is a long-running
loop in the PR path.
