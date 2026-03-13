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

libFuzzer is built into Clang, so you need a Clang compiler. On Nix, use the clang dev shell:

```bash
nix develop .#clang
```

The CMake config (`fuzz/handlers/CMakeLists.txt`) guards the target behind a Clang check — it won't build with GCC or zig-cc.

## Running Locally

### Build the fuzz target

```bash
nix develop .#clang
cmake -S . -B build-fuzz -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fuzz --target iiif_handler_uri_parser_fuzz -j$(nproc)
```

### Run with seed corpus

```bash
cd build-fuzz/fuzz/handlers
mkdir -p corpus
./iiif_handler_uri_parser_fuzz corpus/ ../../../fuzz/handlers/corpus/ -max_total_time=60
```

- First argument (`corpus/`) — live corpus directory. New interesting inputs are written here.
- Second argument (`../../../fuzz/handlers/corpus/`) — seed corpus (read-only). These 52 inputs give the fuzzer a head start with known-good IIIF URIs.
- `-max_total_time=60` — run for 60 seconds. Without this, the fuzzer runs indefinitely (Ctrl+C to stop).

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

```bash
# Limit input size (parser inputs are short URIs, not megabytes)
./iiif_handler_uri_parser_fuzz corpus/ -max_len=256

# Run a fixed number of iterations
./iiif_handler_uri_parser_fuzz corpus/ -runs=100000

# Print coverage stats at the end
./iiif_handler_uri_parser_fuzz corpus/ -print_final_stats=1

# Reproduce a crash (run a single input)
./iiif_handler_uri_parser_fuzz /path/to/crash-abc123
```

## CI Integration

The fuzz workflow (`.github/workflows/fuzz.yml`) runs:

- **Nightly** at 02:00 UTC
- **On demand** via the Actions tab → "fuzz" → "Run workflow" (with configurable duration)

### What it does

1. Builds the fuzz target using `nix develop .#clang`
2. Downloads the corpus from the previous night's run (if available)
3. Runs the fuzzer for 10 minutes (default), merging new findings into the live corpus
4. Uploads the updated corpus as an artifact (`fuzz-corpus`, retained 30 days)
5. On crash:
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
make fuzz-corpus-update
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
3. Add a `CMakeLists.txt` with `-fsanitize=fuzzer,address` flags (copy from `fuzz/handlers/CMakeLists.txt`)
4. Register the subdirectory in `fuzz/CMakeLists.txt`
5. Create a `corpus/` directory with seed inputs
6. Add a step to `.github/workflows/fuzz.yml` for the new target
