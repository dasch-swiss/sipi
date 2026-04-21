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
5. Clean build and test: `just clean && just nix-build` (unit tests run inside the Nix sandbox via `doCheck = enableTests`)

### Adding a new dependency

1. Add `DEP_<NAME>_*` variables to `cmake/dependencies.cmake`
2. Create `ext/<name>/CMakeLists.txt` with the local fallback pattern (see existing deps for examples)
3. Add `add_subdirectory(ext/<name>)` to the root `CMakeLists.txt`
4. Run `just vendor-download` and `just vendor-checksums`
5. Update the SHA-256 hash in the manifest

## Building with Nix (Recommended)

Nix is the single entry point for every Sipi build artifact — dev
binary, static binary, Docker image, debug symbols. It's reproducible,
hermetic, and shares its dependency cache with CI via Cachix.

Full setup and usage: **[Building with Nix](nix.md)** (primer,
one-time setup, all variants, dev-shell inner loop, cross-platform
builds).

Most common commands:

```bash
just nix-build                 # .#dev: Debug + coverage, tests in sandbox
just nix-build-default         # .#default: RelWithDebInfo + tests
just nix-build-sanitized       # .#sanitized: ASan + UBSan
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

## Building on macOS without Nix (Not Recommended)

Building directly on macOS without Nix is unsupported. Use `just nix-build`
or the dev-shell inner loop (see above) instead.

## All `just` targets

Run `just` (no arguments) to see the full list. Key target groups:

| Target | Description |
|--------|-------------|
| `docker-build` | Build Docker image locally |
| `docker-test-build-{amd64,arm64}` | Build + test for specific architecture |
| `test-smoke` | Run smoke tests against the Docker image |
| `nix-build` | `.#dev` — Debug + coverage; unit tests run in the Nix sandbox |
| `nix-build-default` | `.#default` — RelWithDebInfo + tests |
| `nix-build-release` | `.#release` — stripped, no tests |
| `nix-build-sanitized` | `.#sanitized` — Debug + ASan + UBSan |
| `nix-build-fuzz` | `.#fuzz` — libFuzzer harness binary only |
| `nix-build-static-{amd64,arm64}` | `.#static-{amd64,arm64}` — Zig-in-Nix musl |
| `nix-build-release-archive-{amd64,arm64}` | `.#release-archive-{amd64,arm64}` — tarball + sha256 + debug |
| `nix-coverage` | `.#dev^coverage` — writes `result-coverage/coverage.xml` |
| `nix-docker-build` | Stream `.#docker-stream` into local Docker daemon |
| `nix-static-linkage-verify path` | Verify a Linux static binary has no NEEDED entries |
| `nix-macos-dylib-audit path` | Audit macOS sipi runtime dylibs |
| `rust-test-e2e` | Rust end-to-end tests (reads `$SIPI_BIN`) |
| `hurl-test` | Hurl HTTP contract tests (reads `$SIPI_BIN`) |
| `nix-run` | Run sipi with the dev config |
| `nix-valgrind` | Run sipi under Valgrind |
| `nix-run-fuzz corpus duration [seed]` | Run libFuzzer harness against a corpus |
| `vendor-download` | Download all dependency archives to `vendor/` |
| `vendor-verify` | Verify SHA-256 checksums of vendored archives |
| `vendor-checksums` | Print SHA-256 checksums for all archives |
| `kakadu-fetch` | Download Kakadu archive from `dsp-ci-assets` (one-time per version; for Dockerfile only) |
| `docs-build` | Build documentation |
| `docs-serve` | Serve documentation locally |

## Documentation

```bash
just docs-build                # build documentation site
just docs-serve                # serve documentation locally for preview
```
