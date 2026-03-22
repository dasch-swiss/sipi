# Building SIPI from Source Code

## Prerequisites

### Kakadu (JPEG 2000)

To build SIPI from source code, you must have [Kakadu](http://kakadusoftware.com/), a JPEG 2000 development toolkit that is not provided with SIPI and must be licensed separately. The Kakadu source code archive `v8_5-01382N.zip` must be placed in the `vendor` subdirectory of the source tree before building.

### Adobe ICC Color Profiles

SIPI uses the Adobe ICC Color profiles, which are automatically downloaded by the build process. The user is responsible for reading and agreeing with Adobe's license conditions, which are specified in the file `Color Profile EULA.pdf`.

## Vendored Dependencies

SIPI builds all external libraries from source. Source archives are vendored in the `vendor/` directory and tracked with [Git LFS](https://git-lfs.com/). This ensures builds are reproducible and work offline — no internet access is needed during compilation.

### First-time setup (Git LFS)

After cloning the repository, pull the LFS objects:

```
git lfs install   # one-time setup
git lfs pull      # download vendor archives and test images
```

### Dependency management

All dependency metadata (version, URL, SHA-256 hash) is centralized in `cmake/dependencies.cmake`. The build system uses local archives from `vendor/` when present, and falls back to downloading from the URL if not.

| Command                 | Description                                         |
| ----------------------- | --------------------------------------------------- |
| `make vendor-download`  | Download all dependency archives to `vendor/`       |
| `make vendor-verify`    | Verify SHA-256 checksums of all archives            |
| `make vendor-checksums` | Print current checksums (for updating the manifest) |

### Updating a dependency

1. Edit `cmake/dependencies.cmake` — update `DEP_<NAME>_VERSION`, `DEP_<NAME>_URL`, and `DEP_<NAME>_FILENAME`
1. Run `make vendor-download` to fetch the new archive
1. Run `make vendor-checksums` and update `DEP_<NAME>_SHA256` in the manifest
1. Run `make vendor-verify` to confirm
1. Clean build and test: `rm -rf build && make zig-build-local && make zig-test`

### Adding a new dependency

1. Add `DEP_<NAME>_*` variables to `cmake/dependencies.cmake`
1. Create `ext/<name>/CMakeLists.txt` with the local fallback pattern (see existing deps for examples)
1. Add `add_subdirectory(ext/<name>)` to the root `CMakeLists.txt`
1. Run `make vendor-download` and `make vendor-checksums`
1. Update the SHA-256 hash in the manifest

## Building with Docker (Recommended)

The simplest way to build SIPI is using Docker. This requires [Docker](https://www.docker.com/) with [buildx](https://docs.docker.com/buildx/working-with-buildx/) support.

All commands are run from the repository root via `make`:

```
# Build Docker image (compiles SIPI, runs unit tests inside container)
make docker-build

# Run smoke tests against the locally built Docker image
make test-smoke
```

The Docker build uses `ubuntu:24.04` as the base image and installs only the minimal system dependencies needed for compilation. The `Dockerfile` handles cmake configuration, compilation, unit testing, and debug symbol extraction in a multi-stage build.

### Platform-specific builds (used by CI)

```
# Build for specific architectures (used in CI release pipeline)
make docker-test-build-amd64
make docker-test-build-arm64
```

## Building with Zig (Experimental, Parallel to Docker)

Zig-based builds are enabled for local development and static Linux binary production, but Docker remains a first-class release path during rollout. For CI/release workflow details (validation jobs, gates, and artifact publishing), see [CI and Release](https://sipi.io/development/ci/index.md).

### Prerequisites

1. Place the Kakadu archive `v8_5-01382N.zip` in the `vendor/` directory
1. Install Zig `0.15.2`
1. Install build tools (`cmake`, `autoconf`, `automake`, `libtool`)

OpenSSL, libcurl, and libmagic are built from source by SIPI's CMake build; they do not need to be preinstalled as system libraries.

### Local Zig build

```
make zig-build-local
make zig-test
make zig-test-e2e
make zig-run
```

### Static Linux Zig builds

```
make zig-build-amd64   # x86_64-linux-musl
make zig-build-arm64   # aarch64-linux-musl
```

### Static Zig builds in Docker (local CI mirror)

For testing the zig-static build in a Docker container that mirrors the CI build environment (Alpine 3.21 + Zig). Alpine is used because its `/usr/include` contains musl headers natively, avoiding the glibc header contamination that occurs on Ubuntu (where zig cc unconditionally adds `/usr/include`).

```
make zig-static-docker-arm64   # build + unit test aarch64-linux-musl in Docker
make zig-static-docker-amd64   # build + unit test x86_64-linux-musl in Docker
```

These use `Dockerfile.zig-static` which installs Zig, configures the toolchain, builds SIPI, and runs `ctest` — all inside the container. The local targets use `build-static/` as the build directory (CI uses `build/`). E2e tests are not included because the resulting Linux ELF binary cannot run on a macOS host. CI handles the portability proof by running e2e on a bare Ubuntu runner against the Alpine-built binary — see [CI and Release](https://sipi.io/development/ci/index.md) for details.

### Validation commands

```
# Linux static binary must not have dynamic NEEDED entries
# (local Makefile targets use build-static/; CI uses build/)
readelf -d build-static/sipi | grep NEEDED

# Should report static
ldd build-static/sipi

# macOS policy: only libSystem is allowed
otool -L build-zig-macos/sipi
```

## Building with Nix (Native Development)

For native development, SIPI uses [Nix](https://nixos.org/) to provide a reproducible development environment with all required dependencies.

### Setup

1. [Install Nix](https://nixos.org/download.html)
1. Place the Kakadu archive `v8_5-01382N.zip` in the `vendor/` directory
1. Enter the Nix development shell:

```
# GCC environment (default, used by CI)
nix develop

# Clang environment (alternative)
nix develop .#clang
```

### Build and Test

All `nix-*` targets must be run from inside a Nix development shell:

```
# Build SIPI (debug mode with code coverage enabled)
make nix-build

# Run unit tests
make nix-test

# Run end-to-end tests
make nix-test-e2e

# Run all three in sequence (as CI does)
make nix-build && make nix-test && make nix-test-e2e
```

### Run the Server

```
# Start SIPI with the default config
make nix-run
```

### Code Coverage

```
# Generate XML coverage report (gcovr, used by CI/Codecov)
make nix-coverage

# Generate HTML coverage report (lcov, for local viewing)
make nix-coverage-html
```

### Debugging

```
# Run SIPI with Valgrind for memory leak detection
make nix-valgrind
```

## Building on macOS without Zig (Not Recommended)

Building directly on macOS without Nix is unsupported but possible:

```
mkdir -p ./build-mac && cd build-mac && cmake .. && make && ctest --verbose
```

You will need CMake and a C++23-compatible compiler. All library dependencies (including OpenSSL, libcurl, libmagic) are built from source automatically by the build system.

## All Make Targets

Run `make help` to see all available targets:

```
make help
```

Key target groups:

| Target                            | Description                                              |
| --------------------------------- | -------------------------------------------------------- |
| `docker-build`                    | Build Docker image locally                               |
| `docker-test-build-{amd64,arm64}` | Build + test for specific architecture                   |
| `test-smoke`                      | Run smoke tests against Docker image                     |
| `zig-build-local`                 | Build SIPI natively with Zig (experimental)              |
| `zig-test`                        | Run unit tests for Zig local build                       |
| `zig-test-e2e`                    | Run end-to-end tests for Zig local build                 |
| `zig-build-{amd64,arm64}`         | Build static Linux binaries with Zig (experimental)      |
| `zig-static-docker-{arm64,amd64}` | Build + test static binaries in Docker (local CI mirror) |
| `nix-build`                       | Build SIPI natively (debug + coverage)                   |
| `nix-test`                        | Run unit tests                                           |
| `nix-test-e2e`                    | Run end-to-end tests                                     |
| `nix-coverage`                    | Generate XML coverage report                             |
| `nix-run`                         | Run SIPI server                                          |
| `nix-valgrind`                    | Run with Valgrind                                        |
| `docs-build`                      | Build documentation                                      |
| `docs-serve`                      | Serve documentation locally                              |
| `vendor-download`                 | Download all dependency archives to `vendor/`            |
| `vendor-verify`                   | Verify SHA-256 checksums of vendored archives            |
| `vendor-checksums`                | Print SHA-256 checksums for all archives                 |
| `clean`                           | Remove build artifacts                                   |

## Documentation

```
# Build documentation site
make docs-build

# Serve documentation locally for preview
make docs-serve
```
