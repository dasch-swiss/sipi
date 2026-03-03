# Building SIPI from Source Code

## Prerequisites

### Kakadu (JPEG 2000)

To build SIPI from source code, you must have [Kakadu](http://kakadusoftware.com/), a JPEG 2000 development toolkit that is not provided with SIPI and must be licensed separately. The Kakadu source code archive `v8_5-01382N.zip` must be placed in the `vendor` subdirectory of the source tree before building.

### Adobe ICC Color Profiles

SIPI uses the Adobe ICC Color profiles, which are automatically downloaded by the build process. The user is responsible for reading and agreeing with Adobe's license conditions, which are specified in the file `Color Profile EULA.pdf`.

## Building with Docker (Recommended)

The simplest way to build SIPI is using Docker. This requires [Docker](https://www.docker.com/) with [buildx](https://docs.docker.com/buildx/working-with-buildx/) support.

All commands are run from the repository root via `make`:

```
# Build Docker image (compiles SIPI, runs unit tests inside container)
make docker-build

# Run smoke tests against the locally built Docker image
make test-smoke
```

The Docker build uses a pre-built base image (`daschswiss/sipi-base`) that includes all system dependencies. The `Dockerfile` handles cmake configuration, compilation, unit testing, and debug symbol extraction in a multi-stage build.

### Platform-specific builds (used by CI)

```
# Build for specific architectures (used in CI release pipeline)
make docker-test-build-amd64
make docker-test-build-arm64
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

## Building on macOS (Not Recommended)

Building directly on macOS without Nix is unsupported but possible:

```
mkdir -p ./build-mac && cd build-mac && cmake .. && make && ctest --verbose
```

You will need CMake, a C++23-compatible compiler, and all system dependencies (OpenSSL, libcurl, libmagic) installed via Homebrew.

## All Make Targets

Run `make help` to see all available targets:

```
make help
```

Key target groups:

| Target                            | Description                            |
| --------------------------------- | -------------------------------------- |
| `docker-build`                    | Build Docker image locally             |
| `docker-test-build-{amd64,arm64}` | Build + test for specific architecture |
| `test-smoke`                      | Run smoke tests against Docker image   |
| `nix-build`                       | Build SIPI natively (debug + coverage) |
| `nix-test`                        | Run unit tests                         |
| `nix-test-e2e`                    | Run end-to-end tests                   |
| `nix-coverage`                    | Generate XML coverage report           |
| `nix-run`                         | Run SIPI server                        |
| `nix-valgrind`                    | Run with Valgrind                      |
| `docs-build`                      | Build documentation                    |
| `docs-serve`                      | Serve documentation locally            |
| `clean`                           | Remove build artifacts                 |

## Documentation

```
# Build documentation site
make docs-build

# Serve documentation locally for preview
make docs-serve

# Serve doxygen API docs (must build with cmake first)
make nix-doxygen-serve
```
