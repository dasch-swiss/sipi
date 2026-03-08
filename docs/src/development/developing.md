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

## Writing Tests

We use two test frameworks:
[GoogleTest](https://github.com/google/googletest) for unit tests and
[pytest](http://doc.pytest.org/en/latest/) for end-to-end tests.

### Unit Tests

Unit tests live in `test/unit/` and use GoogleTest with ApprovalTests.
Tests are organized by component:

- `test/unit/configuration/` - Configuration parsing tests
- `test/unit/iiifparser/` - IIIF URL parser tests
- `test/unit/sipiimage/` - Image processing tests
- `test/unit/logger/` - Logger tests
- `test/unit/handlers/` - HTTP handler tests

Run all unit tests:

```bash
make nix-test
```

Run a specific test binary directly:

```bash
cd build && test/unit/iiifparser/iiifparser
```

### End-to-End Tests

End-to-end tests live in `test/e2e/` and use pytest. To add tests,
create a Python file whose name begins with `test_` in the `test/e2e/`
directory. The test fixtures in `test/e2e/conftest.py` handle starting
and stopping a SIPI server and provide other testing utilities.

Run e2e tests:

```bash
make nix-test-e2e
```

### Smoke Tests

Smoke tests live in `test/smoke/` and run against a Docker image.
They verify basic server functionality after a Docker build:

```bash
make test-smoke
```

### Approval Tests

Approval tests live in `test/approval/` and use snapshot-based
testing for regression detection.

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
