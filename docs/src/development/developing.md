# Developing SIPI

## Using an IDE

### CLion

If you are using [CLion](https://www.jetbrains.com/clion/), open the
project root and let CLion's Bazel support index the workspace via
the [Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel)
plugin (project from `MODULE.bazel`). Launch CLion from inside the
Nix dev shell so it inherits the build environment:

```bash
nix develop
clion .
```

A direnv setup (see [Nix dev shell](nix.md#direnv)) gives the IDE
the same PATH/env without launching it from a terminal.

## Running locally

A dedicated local development config is provided at
`config/sipi.localdev-config.lua`. It points `imgroot` at the
bundled test images and uses small cache limits (1 MB, 10 files)
so IIIF requests work out of the box and cache eviction is easy
to observe.

### Start the server

```bash
nix develop
just run                                  # bazel build + run sipi server --config localdev
```

The server starts on `http://localhost:1024`.

`just run` depends on `just bazel-build` so the binary at
`bazel-bin/src/cli/sipi` is always rebuilt before the run. After the
first cold build (foreign_cc compiles for openssl/kakadu/libtiff/
etc.), incremental rebuilds re-run only the affected compile + link
through Bazel's per-action cache — typically sub-second through
link.

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

Make several different image requests to fill the cache past its
1 MB / 10 file limits and watch the eviction metrics change:

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

`nix develop` (and direnv-driven shell loads) automatically point
Git at the repo-tracked hook directory `.githooks/` via
`git config core.hooksPath .githooks`. The pre-commit hook runs
`scripts/shttps-context-check.sh` on commits that touch `shttps/`
and refuses commits that introduce a SIPI → shttps leak. Working
outside the dev shell? Run the same `git config` line by hand. The
mandatory gate is CI; the local hook is fast-feedback parity.

## Writing tests

For the authoritative testing strategy (pyramid, layer definitions,
decision tree, IIIF coverage matrix, feature inventory), see
[Testing strategy](testing-strategy.md).

Unit + approval tests use [GoogleTest](https://github.com/google/googletest)
(approval tests additionally use [ApprovalTests.cpp](https://github.com/approvals/ApprovalTests.cpp)).
End-to-end and HTTP-contract tests are written in Rust and live under
`test/e2e-rust/`.

All tests build under Bazel:

```bash
just bazel-test-unit             # bazel test //test/unit/...
just bazel-test-approval         # bazel test //test/approval:approvaltests
just bazel-test-e2e              # all rust_test e2e targets
just bazel-test-smoke            # Docker smoke test
```

CI runs the full pyramid through `just bazel-coverage` (unit +
approval + e2e under instrumentation, lcov for Codecov).

### Unit tests

Unit tests live in `test/unit/` and use GoogleTest with
ApprovalTests. Tests are organized by component:

- `test/unit/cache/` — LRU cache tests
- `test/unit/configuration/` — Configuration parsing tests
- `test/unit/decode_dims/` — Decode-time dimension tests
- `test/unit/filenamehash/` — Filename hashing tests
- `test/unit/handlers/` — HTTP handler tests
- `test/unit/iiifparser/` — IIIF URL parser tests
- `test/unit/logger/` — Logger tests
- `test/unit/memory_budget/` — Decode memory budget tests
- `test/unit/ratelimiter/` — Rate-limiter tests
- `test/unit/shttps/` — HTTP server utility tests
- `test/unit/sipiimage/` — Image processing tests

Per-module Bazel packages co-locate their unit tests alongside the sources
(per ADR-0003). Co-located tests today:

- `//src/observability:connection_metrics_adapter_test` — shttps→sipi metrics adapter tests (was `test/unit/sipiconnectionmetrics/`)
- `//src/metadata:icc_normalize_test` — ICC profile normalization tests (was `test/unit/sipiicc/`)

Run one component:

```bash
bazel test //test/unit/iiifparser:iiifparser_test --test_output=streamed
```

When adding a new unit test, declare a matching `cc_test` target in
`test/unit/<mod>/BUILD.bazel`. CI runs `just bazel-coverage` —
which exercises every `cc_test` under `//test/unit/...` plus
`//test/approval/...` and `//test/e2e-rust/...` in a single pass —
so a missing `cc_test` target = no CI coverage.

### Rust end-to-end tests

Rust e2e tests live in `test/e2e-rust/` and use `reqwest` for HTTP,
`serde_json` for JSON validation, and `insta` for golden snapshots.
They cover IIIF compliance, server behaviour, and upload
functionality.

Run via Bazel — `rules_rust` produces one `rust_test` target per
`tests/<name>.rs`:

```bash
just bazel-test-e2e                                  # full suite (CI canonical)
bazel test //test/e2e-rust:server                    # single target, inner-loop
bazel test //test/e2e-rust:server --test_output=streamed   # see live output
```

The full suite resolves the sipi binary via `$SIPI_BIN`, defaulting
to `bazel-bin/src/cli/sipi`. Override `SIPI_BIN` to point at a
sanitized build (`bazel build --config=asan`) when investigating
ASan findings.

!!! note "Sequential execution"
    Each test starts its own SIPI server on a unique port. The
    `sipi_e2e_test` Bazel macro sets `--test-threads=1` so this
    works out of the box.

### Smoke tests

Smoke tests live in `test/e2e-rust/tests/docker_smoke.rs` and run
against a built Docker image. They verify basic server functionality
after a Docker build:

```bash
just bazel-test-smoke            # builds //src:image, loads tarball, probes
```

### Approval tests

Approval tests live in `test/approval/` and use snapshot-based
testing for regression detection. They run as part of every
`just bazel-test`, `just bazel-coverage`, or focused
`just bazel-test-approval` invocation.

`SOURCE_DATE_EPOCH=946684800` and `SIPI_WORKSPACE_ROOT="."` are
injected by `test/approval/BUILD.bazel`'s `env = {}` block, so the
wall-clock-stamped ICC creation date that lcms2 stamps into JPEG /
PNG / JP2 (and ICC-carrying TIFF) outputs is overwritten with a
fixed value. Without these env vars, the seconds field drifts by
one byte across consecutive runs.

When running the binary directly outside of `bazel test`, export
the same value first:

```bash
SOURCE_DATE_EPOCH=946684800 SIPI_WORKSPACE_ROOT="." \
  ./bazel-bin/test/approval/approvaltests \
  --gtest_filter='ImageEncodeBaseline.*'
```

Without it, expect `.received.*` files for every ICC-touching
test — that's a deliberate test-infrastructure side-effect, not a
regression. See
[`test/approval/CHANGELOG.approval.md`](../../../test/approval/CHANGELOG.approval.md)
for the full list and the re-approval procedure, and `docs/adr/0002-icc-profile-determinism-test-only.md`
in the project root for the design rationale.

## Managing dependencies

Third-party C/C++ libraries are pinned by `http_archive`
declarations in `MODULE.bazel`. Each pin owns its `name`, `urls`,
`sha256`, and any required patches (`patches = []`,
`patch_args = ["-p1"]`). The matching foreign_cc rule lives under
`//ext/<lib>:<lib>` and consumes the archive's `@<lib>//:all_srcs`
filegroup.

To bump a version:

1. Edit the relevant `http_archive(...)` in `MODULE.bazel` — update
   `urls` and clear `sha256`.
2. Run `bazel build //src/cli:sipi` once. Bazel reports the actual
   sha256 in the failure output.
3. Paste the reported `sha256` into the `http_archive` block.
4. Run `bazel build //src/cli:sipi` and `just bazel-test` to confirm.
5. Commit `MODULE.bazel` and `MODULE.bazel.lock`.

Adding a brand-new dependency:

1. Add an `http_archive(...)` block to `MODULE.bazel` with the same
   shape as existing entries.
2. Create `ext/<lib>/BUILD.bazel` declaring the foreign_cc rule
   (`cmake()` / `configure_make()` / `make()`); use a sibling lib
   as the template.
3. Wire it into the consumers (`//src:sipi_lib`, `//shttps:shttps`,
   etc.) via `deps = ["//ext/<lib>:<lib>"]`.

Kakadu is special: it is fetched via a custom `kakadu_archive`
repository_rule (`bazel/kakadu.bzl`) that shells out to
`gh release download`. See [Kakadu setup](kakadu.md).

## Commit message schema

We use [Conventional Commits](https://www.conventionalcommits.org/).
These prefixes drive [release-please](ci.md#release-automation-release-please)
to automatically determine SemVer bumps and generate changelogs —
**using the correct prefix is required, not optional**.

    type(scope): subject
    body

Types:

- `feat` — new feature (SemVer minor)
- `fix` — bug fix (SemVer patch)
- `docs` — documentation changes
- `style` — formatting, no code change
- `refactor` — refactoring production code
- `test` — adding or refactoring tests
- `build` — changes to build system or dependencies
- `chore` — miscellaneous maintenance
- `ci` — continuous integration changes
- `perf` — performance improvements

Breaking changes are indicated with `!`:

    feat!: remove deprecated API endpoint

Example:

    feat(HTTP server): support more authentication methods
