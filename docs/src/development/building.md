# Building SIPI from Source Code

All build commands are defined in the root `justfile`. Run `just` to see
the full list.

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
it succeeds, every build path (Nix, Docker) finds the archive in
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
just bazel-build-sanitized     # bazel build --config=asan --config=ubsan //src:sipi
```

## Building a Docker image

The Docker image is built by `nixpkgs.dockerTools` from the same Nix
derivation that produces every other build artifact. There is no
`Dockerfile` — `flake.nix` is the single source of truth. A running
Docker daemon is still required for `docker load` / `docker push`,
but `docker buildx` is not used.

```bash
just nix-docker-build          # build .#docker-stream, load into local daemon
just test-smoke                # build (if needed) + run Rust testcontainer smoke tests
```

### Platform-specific builds (used by CI)

```bash
just nix-docker-build-amd64    # builds .#packages.x86_64-linux.docker-stream + sipi-debug
just nix-docker-build-arm64    # builds .#packages.aarch64-linux.docker-stream + sipi-debug
just nix-docker-extract-debug amd64    # rename result-debug to sipi-amd64.debug for Sentry
```

The per-arch recipes pin the flake attribute so a wrong-arch runner
fails fast instead of silently producing a mismatched image. They
also realize the matching `sipi-debug` symlink in the same `nix build`
call as a near-free side effect of the layered-image build.

## Building on macOS without Nix (Not Recommended)

Building directly on macOS without Nix is unsupported. Use `just nix-build`
or the dev-shell inner loop (see above) instead.

## All `just` targets

Run `just` (no arguments) to see the full list. Key target groups:

| Target | Description |
|--------|-------------|
| `nix-build` | `.#dev` — Debug + coverage; unit tests run in the Nix sandbox |
| `nix-build-default` | `.#default` — RelWithDebInfo + tests |
| `nix-build-release` | `.#release` — stripped, no tests |
| `bazel-build-sanitized` | `bazel build --config=asan --config=ubsan //src:sipi` — Debug + ASan + UBSan (DWARF inline; `.lsan_suppressions.txt` consulted by the e2e step) |
| `bazel-build-fuzz` | `bazel build --config=fuzz //fuzz/handlers:iiif_handler_uri_parser_fuzz` — libFuzzer harness binary (linux-x86_64 in CI, darwin-aarch64 for local dev; the recipe selects `//tools/fuzz:<host>_fuzz` from `uname`) |
| `nix-coverage` | `.#dev^coverage` — writes `result-coverage/coverage.xml` |
| `nix-docker-build` | Build `.#docker-stream` (host arch), load into local Docker daemon |
| `nix-docker-build-{amd64,arm64}` | Build `.#packages.<arch>-linux.docker-stream` + `.#packages.<arch>-linux.sipi-debug` (single `nix build`) |
| `nix-docker-extract-debug arch` | Rename `result-debug/.../*.debug` to `sipi-<arch>.debug` for Sentry upload |
| `docker-push-{amd64,arm64}` | Push the already-loaded per-arch image to Docker Hub |
| `docker-publish-manifest` | Publish multi-arch manifest combining the two pushed images |
| `test-smoke` | Inner-loop: build Docker image (via Nix), then `cargo test` the smoke suite |
| `nix-test-smoke` | CI canonical: run pre-built `.#smoke-test` binary against an already-loaded image |
| `test-smoke-ci` | Inner-loop: run cargo smoke tests against an already-loaded Docker image |
| `rust-test-e2e` | Inner-loop: cargo-driven Rust end-to-end tests (reads `$SIPI_BIN`) |
| `nix-test-e2e` | CI canonical: run pre-built `.#e2e-tests` binaries via `run-e2e.sh` |
| `hurl-test` | Hurl HTTP contract tests (reads `$SIPI_BIN`) |
| `nix-run` | Run sipi with the dev config |
| `nix-valgrind` | Run sipi under Valgrind |
| `bazel-run-fuzz corpus duration [seed]` | Run libFuzzer harness against a corpus (linux-x86_64 in CI, darwin-aarch64 locally; recipe builds + execs the binary, setting `DYLD_LIBRARY_PATH` on darwin so the toolchain's ASan dylib resolves) |
| `vendor-download` | Download all dependency archives to `vendor/` |
| `vendor-verify` | Verify SHA-256 checksums of vendored archives |
| `vendor-checksums` | Print SHA-256 checksums for all archives |
| `kakadu-fetch` | Download Kakadu archive from `dsp-ci-assets` (dev-shell inner loop only; Nix builds fetch via FOD) |
| `docs-build` | Build documentation |
| `docs-serve` | Serve documentation locally |

## Documentation

```bash
just docs-build                # build documentation site
just docs-serve                # serve documentation locally for preview
```
