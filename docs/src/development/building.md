# Building SIPI from Source Code

All build commands are defined in the root `justfile`. Run `just` to see
the full list. Earlier docs may reference `make` — the Makefile was
removed; every former `make X` is now `just X`.

## Prerequisites

### Kakadu (JPEG 2000)

SIPI uses [Kakadu](http://kakadusoftware.com/), a proprietary JPEG 2000
toolkit licensed separately. The archive is published as a release asset
on the private
[`dasch-swiss/dsp-ci-assets`](https://github.com/dasch-swiss/dsp-ci-assets)
repo and is fetched into `vendor/` by a single command:

```bash
just kakadu-fetch
```

This requires `gh auth login` and `dasch-swiss` org membership. After
it succeeds, every build path (Nix, Docker, Zig) finds the archive in
`vendor/v8_5-01382N.zip`. See [Kakadu setup](kakadu.md) for details
and version bump procedure.

### Adobe ICC Color Profiles

SIPI uses the Adobe ICC Color profiles, which are automatically
downloaded by the build process. The user is responsible for reading
and agreeing with Adobe's license conditions, which are specified in
the file `Color Profile EULA.pdf`.

## Vendored Dependencies

SIPI builds all external libraries from source. Source archives are
vendored in the `vendor/` directory and tracked with
[Git LFS](https://git-lfs.com/). This ensures builds are reproducible
and work offline — no internet access is needed during compilation.

### First-time setup (Git LFS)

After cloning the repository, pull the LFS objects:

```bash
git lfs install   # one-time setup
git lfs pull      # download vendor archives and test images
```

### Dependency management

All dependency metadata (version, URL, SHA-256 hash) is centralized in
`cmake/dependencies.cmake`. The build system uses local archives from
`vendor/` when present, and falls back to downloading from the URL if not.

| Command | Description |
|---------|-------------|
| `just vendor-download` | Download all dependency archives to `vendor/` |
| `just vendor-verify` | Verify SHA-256 checksums of all archives |
| `just vendor-checksums` | Print current checksums (for updating the manifest) |

### Updating a dependency

1. Edit `cmake/dependencies.cmake` — update `DEP_<NAME>_VERSION`, `DEP_<NAME>_URL`, and `DEP_<NAME>_FILENAME`
2. Run `just vendor-download` to fetch the new archive
3. Run `just vendor-checksums` and update `DEP_<NAME>_SHA256` in the manifest
4. Run `just vendor-verify` to confirm
5. Clean build and test: `rm -rf build && just zig-build-local && just zig-test`

### Adding a new dependency

1. Add `DEP_<NAME>_*` variables to `cmake/dependencies.cmake`
2. Create `ext/<name>/CMakeLists.txt` with the local fallback pattern (see existing deps for examples)
3. Add `add_subdirectory(ext/<name>)` to the root `CMakeLists.txt`
4. Run `just vendor-download` and `just vendor-checksums`
5. Update the SHA-256 hash in the manifest

## Building with Nix (Recommended)

`nix` is the single entry point for every Sipi build artifact (dev binary,
static binary, Docker image, debug symbols). It is reproducible, hermetic,
and shares its dependency cache with CI via Cachix.

### One-time setup

1. [Install Nix](https://nixos.org/download.html) (or use
   [Determinate Nix](https://docs.determinate.systems/))
2. Fetch the Kakadu archive: `just kakadu-fetch` (see
   [Kakadu setup](kakadu.md))
3. (Optional) Add the shared binary cache:

   ```bash
   cachix use dasch-swiss
   ```

   This pulls pre-built `ext/` dependencies from CI, reducing first-build
   time from ~15 min to ~2 min.

### Build artifacts

```bash
# Default: RelWithDebInfo binary with separate debug symbols.
nix build .#default
nix build .#default.debug      # extracted symbols at result/lib/debug/...

# Debug build with coverage instrumentation.
nix build .#dev

# Release build, unstripped (for manual distribution).
nix build .#release

# Static Linux binaries (Linux runners only).
nix build .#static-amd64
nix build .#static-arm64

# Release archives (.tar.gz + .sha256 + .debug).
nix build .#release-archive-amd64
nix build .#release-archive-arm64

# Docker images (Linux runners only).
nix build .#docker             # writes to /nix/store
just nix-docker-build          # streams into the local Docker daemon
```

### Dev shell + iterative build

For day-to-day work, `nix develop` provides the full toolchain (clang19
+ libc++, cmake, autoconf, just, gcovr, lcov, hurl, …):

```bash
nix develop                    # default = clang
nix develop .#fuzz             # libstdc++ for libFuzzer ABI
nix develop .#gcc              # gcc14Stdenv

# Inside the shell:
just nix-build                 # debug + coverage build into ./build/
just nix-test                  # GoogleTest unit + ApprovalTests
just rust-test-e2e             # Rust e2e harness
just hurl-test                 # Hurl HTTP contract tests
just nix-coverage              # XML coverage report (Codecov format)
just nix-coverage-html         # HTML coverage report (lcov)
just nix-run                   # start sipi against the default config
just nix-valgrind              # run sipi under valgrind
```

## Building with Docker

The Dockerfile produces the production Docker image. It uses
`ubuntu:24.04` as the base image and handles cmake configuration,
compilation, unit testing, and debug symbol extraction in a multi-stage
build.

```bash
just docker-build              # build image (compiles + unit tests)
just test-smoke                # smoke tests against the Docker image
```

### Platform-specific builds (used by CI)

```bash
just docker-test-build-amd64
just docker-test-build-arm64
```

## Building with Zig

Zig produces statically linked Linux binaries via `cmake/zig-toolchain.cmake`.
This is the path used today for static release artifacts and the macOS
dylib audit; the Nix-based static builds (`.#static-{amd64,arm64}`) are
validated alongside Zig and will replace it in a future phase.

### Prerequisites

1. Run `just kakadu-fetch` to populate `vendor/v8_5-01382N.zip` (see
   [Kakadu setup](kakadu.md))
2. Install Zig `0.15.2`
3. Install build tools (`cmake`, `autoconf`, `automake`, `libtool`)

OpenSSL, libcurl, and libmagic are built from source by SIPI's CMake
build; they do not need to be preinstalled as system libraries.

### Local Zig build

```bash
just zig-build-local
just zig-test
just zig-test-e2e
just zig-run
```

### Static Linux Zig builds

```bash
just zig-build-amd64           # x86_64-linux-musl
just zig-build-arm64           # aarch64-linux-musl
```

### Static Zig builds in Docker (local CI mirror)

For testing the zig-static build in a Docker container that mirrors the
CI build environment (Alpine 3.21 + Zig). Alpine is used because its
`/usr/include` contains musl headers natively, avoiding the glibc header
contamination that occurs on Ubuntu (where zig cc unconditionally adds
`/usr/include`).

```bash
just zig-static-docker-arm64   # build + unit test aarch64-linux-musl in Docker
just zig-static-docker-amd64   # build + unit test x86_64-linux-musl in Docker
```

These use `Dockerfile.zig-static` which installs Zig, configures the
toolchain, builds SIPI, and runs `ctest` — all inside the container.
The local targets use `build-static/` as the build directory (CI uses
`build/`). E2e tests are not included because the resulting Linux ELF
binary cannot run on a macOS host. CI handles the portability proof by
running e2e on a bare Ubuntu runner against the Alpine-built binary —
see [CI and Release](ci.md) for details.

### Validation commands

```bash
# Linux static binary must not have dynamic NEEDED entries
# (local justfile targets use build-static/; CI uses build/)
readelf -d build-static/sipi | grep NEEDED

# Should report static
ldd build-static/sipi

# macOS policy: only libSystem is allowed
otool -L build-zig-macos/sipi
```

## Building on macOS without Nix or Zig (Not Recommended)

Building directly on macOS without Nix is unsupported but possible:

```bash
mkdir -p ./build-mac && cd build-mac && cmake .. && make && ctest --verbose
```

You will need CMake and a C++23-compatible compiler. All library
dependencies (including OpenSSL, libcurl, libmagic) are built from
source automatically by the build system.

## All `just` targets

Run `just` (no arguments) to see the full list. Key target groups:

| Target | Description |
|--------|-------------|
| `docker-build` | Build Docker image locally |
| `docker-test-build-{amd64,arm64}` | Build + test for specific architecture |
| `test-smoke` | Run smoke tests against the Docker image |
| `nix-build` | Build SIPI natively (debug + coverage) — inside `nix develop` |
| `nix-test` | Run unit tests |
| `rust-test-e2e` | Run Rust end-to-end tests |
| `hurl-test` | Run Hurl HTTP contract tests |
| `nix-coverage` | Generate XML coverage report |
| `nix-coverage-html` | Generate HTML coverage report |
| `nix-run` | Run SIPI server |
| `nix-valgrind` | Run with Valgrind |
| `nix-build-release` | Run `nix build .#default` |
| `nix-build-static-{amd64,arm64}` | Run `nix build .#static-{amd64,arm64}` |
| `nix-docker-build` | Stream `nix build .#docker-stream` into local Docker daemon |
| `zig-build-local` | Build SIPI natively with Zig |
| `zig-test` | Run unit tests for Zig local build |
| `zig-test-e2e` | Run end-to-end tests for Zig local build |
| `zig-build-{amd64,arm64}` | Build static Linux binaries with Zig |
| `zig-static-docker-{arm64,amd64}` | Build + test static binaries in Docker (local CI mirror) |
| `vendor-download` | Download all dependency archives to `vendor/` |
| `vendor-verify` | Verify SHA-256 checksums of vendored archives |
| `vendor-checksums` | Print SHA-256 checksums for all archives |
| `kakadu-fetch` | Download Kakadu archive from `dsp-ci-assets` (one-time per version) |
| `docs-build` | Build documentation |
| `docs-serve` | Serve documentation locally |

## Documentation

```bash
just docs-build                # build documentation site
just docs-serve                # serve documentation locally for preview
```
