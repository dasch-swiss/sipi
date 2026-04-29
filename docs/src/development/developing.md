# Developing SIPI

## Using an IDE

### CLion

If you are using the [CLion](https://www.jetbrains.com/clion/) IDE, note
that code introspection in the CLion editor may not work until it has
run CMake. Open the project root directory (which contains `CMakeLists.txt`)
and let CLion configure the project automatically.

For Nix-based development, launch CLion from inside the Nix shell so it
inherits all required environment variables and dependencies:

```bash
nix develop
clion .
```

## Running Locally

A dedicated local development config is provided at
`config/sipi.localdev-config.lua`. It points `imgroot` at the bundled test
images and uses small cache limits (1 MB, 10 files) so IIIF requests work
out of the box and cache eviction is easy to observe.

### Start the server

```bash
# Reproducible (what CI runs) — build through Nix, then run the built binary
just nix-build
just nix-run
```

Or, for the inner-loop dev workflow (non-reproducible — does not match CI
outputs byte-for-byte):

```bash
nix develop
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCODE_COVERAGE=ON
cmake --build build --parallel
./build/sipi --config config/sipi.localdev-config.lua
```

The server starts on `http://localhost:1024`.

### Try some requests

```bash
# Fetch an IIIF image with a transformation (creates a cache entry)
# Note: requests that need no processing (same format, full size, no rotation)
# are served directly from the original file and bypass the cache.
curl http://localhost:1024/unit/gradient-stars.tif/full/max/0/default.jpg -o /tmp/test.jpg

# Prometheus metrics (cache counters, gauges, no auth required)
curl http://localhost:1024/metrics

# Cache file list via Lua API (requires admin credentials from config)
curl -u admin:Sipi-Admin http://localhost:1024/api/cache
```

Make several different image requests to fill the cache past its 1 MB / 10 file
limits and watch the eviction metrics change:

```bash
# Format conversions (TIF → JPG) trigger caching — all well under 2 MB
curl http://localhost:1024/unit/gradient-stars.tif/full/max/0/default.jpg -o /dev/null
curl http://localhost:1024/unit/lena512.tif/full/max/0/default.jpg -o /dev/null
curl http://localhost:1024/unit/cielab.tif/full/max/0/default.jpg -o /dev/null

# Resized requests also trigger caching
curl http://localhost:1024/unit/MaoriFigure.jpg/full/200,/0/default.jpg -o /dev/null
curl http://localhost:1024/unit/MaoriFigureWatermark.jpg/full/200,/0/default.jpg -o /dev/null

curl http://localhost:1024/metrics | grep sipi_cache
```

### Available configs

| Config file | Purpose |
|-------------|---------|
| `config/sipi.config.lua` | Production-like defaults (`./images` imgroot, 20 MB cache) |
| `config/sipi.localdev-config.lua` | Local development (test images, tiny cache, DEBUG logging) |
| `config/sipi.test-config.lua` | Automated test suite |

### Pre-commit hook

`nix develop` (and direnv-driven shell loads) automatically point Git at the
repo-tracked hook directory `.githooks/` via `git config core.hooksPath
.githooks`. The pre-commit hook runs `scripts/shttps-context-check.sh` on
commits that touch `shttps/` and refuses commits that introduce a SIPI→shttps
leak. Working outside the dev shell? Run the same `git config` line by hand.
The mandatory gate is CI; the local hook is fast-feedback parity.

## Writing Tests

We use two test frameworks:
[GoogleTest](https://github.com/google/googletest) for unit tests and
[pytest](http://doc.pytest.org/en/latest/) for end-to-end tests.

### Unit Tests

Unit tests live in `test/unit/` and use GoogleTest with ApprovalTests.
Tests are organized by component:

- `test/unit/cache/` - LRU cache tests
- `test/unit/configuration/` - Configuration parsing tests
- `test/unit/decode_dims/` - Decode-time dimension tests
- `test/unit/filenamehash/` - Filename hashing tests
- `test/unit/handlers/` - HTTP handler tests
- `test/unit/iiifparser/` - IIIF URL parser tests
- `test/unit/logger/` - Logger tests
- `test/unit/memory_budget/` - Decode memory budget tests
- `test/unit/ratelimiter/` - Rate-limiter tests
- `test/unit/shttps/` - HTTP server utility tests
- `test/unit/sipiicc/` - ICC profile normalization tests (`SOURCE_DATE_EPOCH` helper)
- `test/unit/sipiimage/` - Image processing tests

Run all unit tests (inside the Nix sandbox via `doCheck = enableTests` in
`package.nix` — `just nix-build` fails if any unit test fails):

```bash
just nix-build
```

Run a specific test binary directly, from the dev-shell inner loop:

```bash
nix develop
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCODE_COVERAGE=ON
cmake --build build --parallel
cd build && test/unit/iiifparser/iiifparser
```

### Rust End-to-End Tests

Rust-based e2e tests live in `test/e2e-rust/` and use `reqwest` for HTTP
requests, `serde_json` for JSON validation, and `insta` for golden baseline
snapshots. They cover IIIF compliance, server behaviour, and upload
functionality.

Two recipes run the same test code via two different build paths:

```bash
# CI-canonical path: consume pre-built test binaries from the
# `.#e2e-tests` Nix derivation (Cachix-cacheable, no cargo on PATH
# required). This is what every CI job runs.
just nix-test-e2e

# Inner-loop dev path: build and run via `cargo test` in the dev
# shell. Faster for iterating on the test code itself; not used by CI.
just rust-test-e2e
```

Both recipes resolve the sipi binary via `$SIPI_BIN` with a canonical
default of `./result/bin/sipi` (the Nix artifact path). Override
`SIPI_BIN` to point at a dev-shell-built binary if you are iterating on
cmake locally.

!!! note "Sequential execution required"
    Tests must run with `--test-threads=1` because each test starts its own
    SIPI server instance on a unique port. Both recipes handle this
    automatically.

### Hurl HTTP Contract Tests

Declarative HTTP contract tests live in `test/hurl/` and use
[Hurl](https://hurl.dev). Each `.hurl` file describes a sequence of HTTP
requests and expected responses.

Run Hurl tests:

```bash
just hurl-test
```

Current test files:

- `file_access.hurl` — File access and permission checks
- `health.hurl` — `/health` liveness/version/uptime contract
- `iiif_transform.hurl` — IIIF Image API 3.0 transform + canonical-redirect contract
- `info_json.hurl` — IIIF info.json structure and conformance fields
- `lua_endpoints.hurl` — Lua script endpoint responses
- `metrics.hurl` — Prometheus `/metrics` exposition contract
- `missing_sidecar.hurl` — Behaviour when sidecar files are absent
- `sqlite_api.hurl` — SQLite API endpoint tests
- `video_knora_json.hurl` — Video metadata JSON responses

!!! note "Requires Hurl binary"
    Hurl is available inside `nix develop`. Outside Nix, install it from
    [hurl.dev](https://hurl.dev).

### Smoke Tests

Smoke tests live in `test/e2e-rust/tests/docker_smoke.rs` and run
against a built Docker image. They verify basic server functionality
after a Docker build:

```bash
# Inner-loop dev path: builds the image via Nix, then runs cargo test.
just test-smoke

# CI canonical path: builds the image via Nix, then runs the
# pre-built smoke binary from the .#smoke-test derivation
# (no cargo on PATH required).
just nix-test-smoke
```

### Approval Tests

Approval tests live in `test/approval/` and use snapshot-based
testing for regression detection. Run them via ctest:

```bash
cd build
ctest -L approval --output-on-failure
```

CMake injects `SOURCE_DATE_EPOCH=946684800` into the `sipi.approvaltests`
invocation so the wall-clock-stamped ICC creation date that lcms2 stamps
into JPEG / PNG / JP2 (and ICC-carrying TIFF) outputs is overwritten
with a fixed value. Without the env var the seconds field drifts by one
byte across consecutive runs.

When running the binary directly outside of ctest, export the same
value first:

```bash
cd build/test/approval
SOURCE_DATE_EPOCH=946684800 ./sipi.approvaltests \
  --gtest_filter='ImageEncodeBaseline.*'
```

Without it, expect `.received.*` files for every ICC-touching test —
that's a deliberate test-infrastructure side-effect, not a regression.
See [`test/approval/CHANGELOG.approval.md`](../../../test/approval/CHANGELOG.approval.md)
for the full list and the re-approval procedure.

## Managing Dependencies

External library sources are vendored in `vendor/` and tracked with Git LFS.
The manifest `cmake/dependencies.cmake` is the single source of truth for
versions, download URLs, and SHA-256 hashes.

See [Building: Vendored Dependencies](building.md#vendored-dependencies)
for setup instructions and update/add workflows.

Quick reference:

```bash
make vendor-download    # fetch all archives
make vendor-verify      # check SHA-256 integrity
make vendor-checksums   # print hashes for manifest updates
```

## Commit Message Schema

We use [Conventional Commits](https://www.conventionalcommits.org/).
These prefixes drive [release-please](ci.md#release-automation-release-please)
to automatically determine SemVer bumps and generate changelogs — **using the
correct prefix is required, not optional**.

    type(scope): subject
    body

Types:

- `feat` - new feature (SemVer minor)
- `fix` - bug fix (SemVer patch)
- `docs` - documentation changes
- `style` - formatting, no code change
- `refactor` - refactoring production code
- `test` - adding or refactoring tests
- `build` - changes to build system or dependencies
- `chore` - miscellaneous maintenance
- `ci` - continuous integration changes
- `perf` - performance improvements

Breaking changes are indicated with `!`:

    feat!: remove deprecated API endpoint

Example:

    feat(HTTP server): support more authentication methods
