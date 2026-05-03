# Fuzz Testing

Sipi uses [libFuzzer](https://llvm.org/docs/LibFuzzer.html) to fuzz-test the IIIF URI parser (`parse_iiif_uri`). The fuzzer feeds random and mutated inputs to the parser, looking for crashes, memory safety issues, and undefined behavior via AddressSanitizer.

## Architecture

```
fuzz/
└── handlers/
    ├── BUILD.bazel              # cc_library(fuzz_subset) + cc_binary
    ├── iiif_handler_uri_parser_target.cpp  # Fuzz harness
    └── corpus/                  # Seed corpus (checked into git)
        ├── BUILD.bazel          # filegroup(seed_corpus) — runfile of cc_binary
        ├── iiif_basic           # /prefix/image.jp2/full/max/0/default.jpg
        ├── info_json            # /unit/lena512.jp2/info.json
        ├── knora_json           # /unit/lena512.jp2/knora.json
        └── ...                  # 52 seed inputs total
```

The Bazel build also wires:

- `tools/fuzz/BUILD.bazel` — `constraint_setting`/`constraint_value` plus the `linux_x86_64_fuzz` platform that `--config=fuzz` targets.
- `MODULE.bazel` — registers a second `llvm_toolchain_fuzz` (libstdc++) sharing `@llvm_toolchain_llvm` via `llvm.toolchain_root` and gated by `extra_target_compatible_with = ["//tools/fuzz:fuzz_enabled"]`.
- `.bazelrc` `build:fuzz` — pins `--platforms=//tools/fuzz:linux_x86_64_fuzz`, injects `-fsanitize=fuzzer-no-link` + `-fsanitize-coverage=trace-cmp` + ASan, and adds `-fsanitize=fuzzer` at link.

The fuzz harness is minimal — it converts the fuzzer's byte input to a `std::string` and calls `parse_iiif_uri()`:

```cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  std::string input(reinterpret_cast<const char *>(Data), Size);
  auto result = handlers::iiif_handler::parse_iiif_uri(input);
  if (result.has_value()) {
    volatile auto type = result->request_type;
    (void)type;
  }
  return 0;
}
```

## Requirements

libFuzzer ships in Clang's compiler-rt. The build uses two registered
LLVM toolchains, both sharing the same LLVM 19.1.7 download:

* On linux-x86_64, `@llvm_toolchain_fuzz` wins (libstdc++, mirroring
  today's `pkgs.llvmPackages_19.stdenv` override). The platform
  `//tools/fuzz:linux_x86_64_fuzz` carries the `fuzz_enabled` constraint
  the toolchain is gated on.
* On darwin-aarch64, the default `@llvm_toolchain` wins (Apple SDK +
  libc++, which is libFuzzer's ABI on darwin anyway). The platform
  `//tools/fuzz:darwin_aarch64_fuzz` activates the harness's
  `target_compatible_with = ["//tools/fuzz:fuzz_enabled"]` gate.

The `bazel-build-fuzz` and `bazel-run-fuzz` justfile recipes detect the
host via `uname` and select the matching platform automatically. linux-
aarch64 is out of scope — the deployment target is amd64 only and fuzz
CI never runs there. Other hosts get a clear error message.

`bazel-run-fuzz` execs the binary directly (after a build step) rather
than going through `bazel run`, because Apple's ASan runtime
(`libclang_rt.asan_osx_dynamic.dylib`) is dynamically linked via
`@rpath` and macOS SIP strips `DYLD_LIBRARY_PATH` across the
`bazel run` subprocess chain. The recipe sets `DYLD_LIBRARY_PATH` in
its own shell on darwin and then `exec`s the binary.

## Running Locally

### Build the fuzz target

```bash
just bazel-build-fuzz
```

This wraps `bazel build --config=fuzz //fuzz/handlers:iiif_handler_uri_parser_fuzz`
and emits `bazel-bin/fuzz/handlers/iiif_handler_uri_parser_fuzz`.

### Run with seed corpus

```bash
mkdir -p corpus-live
just bazel-run-fuzz corpus-live 60 fuzz/handlers/corpus
```

- First positional arg (`corpus-live`) — live corpus directory. New interesting inputs are written here.
- Second positional arg (`60`) — duration in seconds. Without a bound the fuzzer runs indefinitely (Ctrl+C to stop).
- Third positional arg (`fuzz/handlers/corpus`) — optional read-only seed corpus. These 52 inputs give the fuzzer a head start with known-good IIIF URIs.

The recipe forwards libFuzzer's exit code so a crash propagates through `just`. It also appends `-print_final_stats=1` automatically.

### Understanding the output

```
INFO: Loaded 52 files from ../../../fuzz/handlers/corpus/
#52    INITED cov: 623 ft: 1439 corp: 35/1066b
#55    NEW    cov: 625 ft: 1441 corp: 36/1104b
#96    REDUCE cov: 670 ft: 1589 corp: 46/1461b
```

| Field | Meaning |
|-------|---------|
| `cov` | Code coverage edges discovered — should grow initially, then plateau |
| `ft` | Feature targets — finer-grained coverage metric |
| `corp` | Corpus size / total bytes — grows as new interesting inputs are found |
| `NEW` | Found an input that triggers new coverage |
| `REDUCE` | Found a smaller input that triggers the same coverage |
| `pulse` | Periodic heartbeat — the fuzzer is still running |

### Useful flags

For anything beyond duration + seed corpus, invoke the fuzzer binary directly (the `just` recipe is a thin convenience wrapper, not a full passthrough):

```bash
# Limit input size (parser inputs are short URIs, not megabytes)
./bazel-bin/fuzz/handlers/iiif_handler_uri_parser_fuzz corpus-live/ -max_len=256

# Run a fixed number of iterations
./bazel-bin/fuzz/handlers/iiif_handler_uri_parser_fuzz corpus-live/ -runs=100000

# Reproduce a crash (run a single input)
./bazel-bin/fuzz/handlers/iiif_handler_uri_parser_fuzz /path/to/crash-abc123
```

## CI Integration

The fuzz workflow (`.github/workflows/fuzz.yml`) runs:

- **Nightly** at 02:00 UTC
- **On demand** via the Actions tab → "fuzz" → "Run workflow" (with configurable duration)

### What it does

1. Builds the fuzz target via `just bazel-build-fuzz` (→ `bazel build --config=fuzz //fuzz/handlers:iiif_handler_uri_parser_fuzz`)
2. Downloads the corpus from the previous night's run (if available)
3. Runs `just bazel-run-fuzz fuzz-corpus-live $FUZZ_DURATION fuzz/handlers/corpus` (default 10 minutes), merging new findings into the live corpus
4. Uploads the updated corpus as an artifact (`fuzz-corpus`, retained 30 days)
5. On crash (libFuzzer exits non-zero; `set -o pipefail` propagates it):
    - Uploads crash reproducers as artifacts (`fuzz-crashes`, retained 90 days)
    - Opens a GitHub issue with crash details, hex dump, and reproduction instructions

### Corpus accumulation

Each nightly run picks up where the last one left off. The corpus grows over time, so the fuzzer spends its time exploring new territory rather than rediscovering known paths.

```
Night 1: 52 seeds → 10 min → ~200 inputs (uploaded)
Night 2: ~200 inputs → 10 min → ~300 inputs (uploaded)
Night N: corpus keeps growing, coverage accumulates
```

## Updating the Seed Corpus

Periodically, you should pull the CI corpus back into the repo so that:

- Local fuzzing starts with the best available coverage
- The CI corpus survives artifact expiration (30-day retention)
- New contributors get a rich starting corpus

### Download and merge

```bash
just fuzz-corpus-update
```

This downloads the latest `fuzz-corpus` artifact from CI, deduplicates by content hash, and merges into `fuzz/handlers/corpus/`. It reports how many new inputs were added.

Then commit the result:

```bash
git add fuzz/handlers/corpus/
git commit -m "test: update fuzz seed corpus from CI"
```

### When to update

- After the fuzzer has been running for a few weeks and coverage has grown significantly
- Before a release, to lock in the best available corpus
- After fixing a bug found by the fuzzer (the crash input is automatically in the CI corpus)

## Adding New Fuzz Targets

To fuzz a different component:

1. Create a new directory under `fuzz/` (e.g., `fuzz/image_processing/`)
2. Write a harness implementing `LLVMFuzzerTestOneInput`
3. Add a `BUILD.bazel` with a `cc_library` (the explicit subset of first-party + ext deps the harness exercises) plus a `cc_binary` carrying `target_compatible_with = ["//tools/fuzz:fuzz_enabled"]` (copy from `fuzz/handlers/BUILD.bazel`)
4. Create a `corpus/` directory with seed inputs and a `BUILD.bazel` exposing them as `filegroup(seed_corpus)`
5. Add a step to `.github/workflows/fuzz.yml` for the new target
