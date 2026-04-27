# Fuzz Testing

Sipi uses [libFuzzer](https://llvm.org/docs/LibFuzzer.html) to fuzz-test the IIIF URI parser (`parse_iiif_uri`). The fuzzer feeds random and mutated inputs to the parser, looking for crashes, memory safety issues, and undefined behavior via AddressSanitizer.

## Architecture

```
fuzz/
├── CMakeLists.txt              # Top-level fuzz build (adds subdirectories)
└── handlers/
    ├── CMakeLists.txt           # Fuzz target build config (requires Clang)
    ├── iiif_handler_uri_parser_target.cpp  # Fuzz harness
    └── corpus/                  # Seed corpus (checked into git)
        ├── iiif_basic           # /prefix/image.jp2/full/max/0/default.jpg
        ├── info_json            # /unit/lena512.jp2/info.json
        ├── knora_json           # /unit/lena512.jp2/knora.json
        └── ...                  # 52 seed inputs total
```

The fuzz harness is minimal — it converts the fuzzer's byte input to a `std::string` and calls `parse_iiif_uri()`:

```
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

libFuzzer is built into Clang, so the fuzz variant uses `llvmPackages_19.stdenv` under the hood. Nix handles the toolchain — no dev-shell dance required.

The CMake config (`fuzz/handlers/CMakeLists.txt`) guards the target behind a Clang check; it won't build with GCC or zig-cc, but `.#fuzz` pins Clang so that path never matters in practice.

## Running Locally

### Build the fuzz target

```
just nix-build-fuzz
```

This wraps `nix build .#fuzz` and emits `result/bin/iiif_handler_uri_parser_fuzz`.

### Run with seed corpus

```
mkdir -p corpus-live
just nix-run-fuzz corpus-live 60 fuzz/handlers/corpus
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

| Field    | Meaning                                                               |
| -------- | --------------------------------------------------------------------- |
| `cov`    | Code coverage edges discovered — should grow initially, then plateau  |
| `ft`     | Feature targets — finer-grained coverage metric                       |
| `corp`   | Corpus size / total bytes — grows as new interesting inputs are found |
| `NEW`    | Found an input that triggers new coverage                             |
| `REDUCE` | Found a smaller input that triggers the same coverage                 |
| `pulse`  | Periodic heartbeat — the fuzzer is still running                      |

### Useful flags

For anything beyond duration + seed corpus, invoke the fuzzer binary directly (the `just` recipe is a thin convenience wrapper, not a full passthrough):

```
# Limit input size (parser inputs are short URIs, not megabytes)
./result/bin/iiif_handler_uri_parser_fuzz corpus-live/ -max_len=256

# Run a fixed number of iterations
./result/bin/iiif_handler_uri_parser_fuzz corpus-live/ -runs=100000

# Reproduce a crash (run a single input)
./result/bin/iiif_handler_uri_parser_fuzz /path/to/crash-abc123
```

## CI Integration

The fuzz workflow (`.github/workflows/fuzz.yml`) runs:

- **Nightly** at 02:00 UTC
- **On demand** via the Actions tab → "fuzz" → "Run workflow" (with configurable duration)

### What it does

1. Builds the fuzz target via `just nix-build-fuzz` (→ `nix build .#fuzz`)
1. Downloads the corpus from the previous night's run (if available)
1. Runs `just nix-run-fuzz fuzz-corpus-live $FUZZ_DURATION fuzz/handlers/corpus` (default 10 minutes), merging new findings into the live corpus
1. Uploads the updated corpus as an artifact (`fuzz-corpus`, retained 30 days)
1. On crash (libFuzzer exits non-zero; `set -o pipefail` propagates it):
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

```
just fuzz-corpus-update
```

This downloads the latest `fuzz-corpus` artifact from CI, deduplicates by content hash, and merges into `fuzz/handlers/corpus/`. It reports how many new inputs were added.

Then commit the result:

```
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
1. Write a harness implementing `LLVMFuzzerTestOneInput`
1. Add a `CMakeLists.txt` with `-fsanitize=fuzzer,address` flags (copy from `fuzz/handlers/CMakeLists.txt`)
1. Register the subdirectory in `fuzz/CMakeLists.txt`
1. Create a `corpus/` directory with seed inputs
1. Add a step to `.github/workflows/fuzz.yml` for the new target
