# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SIPI (Simple Image Presentation Interface) is a multithreaded, high-performance, IIIF-compatible media server written in C++23. It implements IIIF Image API 3.0 and provides efficient image format conversions while preserving metadata. The server can be used both as a command-line tool and as a web server with Lua scripting support.

## Domain Model

Read these before reasoning about names, boundaries, or architectural decisions:

- [`UBIQUITOUS_LANGUAGE.md`](UBIQUITOUS_LANGUAGE.md) — canonical SIPI glossary. Defines Image vs Bitstream, the IIIF pipeline terms (Region / Size / Rotation / Quality / Format / Decode level / Canonical URL / Cache key), Preservation metadata, the three Lua entry points, the seven Permission types, and more. Use these terms in code comments, commit messages, and PR descriptions; aliases listed there are *avoid*.
- [`CONTEXT-MAP.md`](CONTEXT-MAP.md) — declares two bounded contexts ([SIPI image server](CONTEXT.md) + [shttps embedded HTTP framework](shttps/CONTEXT.md)) with strict one-way SIPI → shttps dependency direction. Lists the four primary seam types and the secondary surface scheduled for re-homing.
- [`docs/adr/`](docs/adr/) — architectural decision records. Start with [`0001-shttps-as-strangler-fig-target.md`](docs/adr/0001-shttps-as-strangler-fig-target.md).

## Build System and Common Commands

All targets are in a single `justfile`. Run `just` for a complete list.
For full build instructions (Docker, Nix, macOS), see [`docs/src/development/building.md`](docs/src/development/building.md).

**Build reproducibility invariant:** every `nix-*` recipe wraps `nix build .#<variant>`. CI invokes only `just <recipe>` — no inline cmake or `nix build` calls. Incremental inner-loop development is a documented dev-shell pattern (see below), not a recipe.

**Build completeness invariant:** every build target must succeed on every supported platform — macOS (darwin-aarch64), linux-x86_64, and linux-aarch64. This applies to `.#dev`, `.#default`, `.#release`, `.#sanitized`, and `.#fuzz` on all platforms; and to `.#docker`, `.#docker-stream`, and `.#sipi-debug` on Linux only (Linux-only outputs are gated by `pkgs.lib.optionalAttrs isLinux` in `flake.nix`). A target that builds on only some of its supported platforms is a shipping bug, not a known limitation — CI is Linux-only, so **a green CI run does not verify macOS**. Before shipping any change to `flake.nix`, `package.nix`, or a `justfile` build recipe:

1. Run every affected variant on macOS locally (`just nix-build-<variant>`).
2. Run every affected Linux variant locally via Determinate's native-linux-builder (`nix build .#packages.x86_64-linux.<variant>` and `.#packages.aarch64-linux.<variant>`). See [`docs/src/development/nix.md`](docs/src/development/nix.md) for setup.
3. Pay special attention to `overrideAttrs` blocks that replace `buildPhase` / `installPhase` — those encode assumptions about nixpkgs hook behavior (e.g. whether the cmake hook leaves CWD at `$sourceRoot` or `$sourceRoot/build`) that are platform- and version-sensitive. Prefer CWD-robust patterns (`[ -f CMakeCache.txt ] || cd "${cmakeBuildDir:-build}"`) over hard-coded paths.

If a change can't be verified on a platform locally, say so explicitly and gate the merge on a corresponding CI check being added.

**First-time setup for the dev-shell inner loop:** run `just kakadu-fetch` once to download the proprietary Kakadu archive from `dsp-ci-assets` into `vendor/`. Nix builds (including `just nix-docker-build`) fetch Kakadu directly via the flake's FOD (no `vendor/` step). Requires `gh auth login` and `dasch-swiss` org membership. See [`docs/src/development/kakadu.md`](docs/src/development/kakadu.md).

**ICC determinism invariant:** [`SipiIcc::iccBytes()`](src/metadata/SipiIcc.cpp) is the single chokepoint that converts an `cmsHPROFILE` into raw bytes for codec consumption — every TIFF, JPEG, PNG, and JP2 emission funnels through it. Any new format handler must route through `iccBytes()`; bypassing it (calling `cmsSaveProfileToMem` directly) breaks the approval-test gate. Approval tests run with `SOURCE_DATE_EPOCH` injected by CMake (see [`test/approval/CMakeLists.txt`](test/approval/CMakeLists.txt)) so the wall-clock-stamped ICC creation date is overwritten with a fixed value and goldens stay byte-deterministic. Production never sets the env var; deployed binaries continue to embed wall-clock-stamped ICC headers. See [`docs/adr/0002-icc-profile-determinism-test-only.md`](docs/adr/0002-icc-profile-determinism-test-only.md).

### Quick Reference

```bash
# Nix build (reproducible — this is what CI runs)
just nix-build                   # .#dev: Debug + coverage, tests run in sandbox
just nix-build-default           # .#default: RelWithDebInfo + tests (matches distributed binary shape)
just nix-build-release           # .#release: stripped, no tests
just nix-build-sanitized         # .#sanitized: Debug + ASan + UBSan, tests run in sandbox
just nix-build-fuzz              # .#fuzz: libFuzzer harness binary only
just nix-coverage                # .#dev^coverage: produces result-coverage/coverage.xml
just nix-docker-build            # .#docker-stream: host-arch image, load into local daemon
just nix-docker-build-amd64      # .#packages.x86_64-linux.docker-stream + sipi-debug (CI)
just nix-docker-build-arm64      # .#packages.aarch64-linux.docker-stream + sipi-debug (CI)
just nix-docker-extract-debug arch  # rename result-debug/.../*.debug → sipi-<arch>.debug

# Tests (consume $SIPI_BIN, default ./result/bin/sipi)
just nix-test-e2e                # Rust e2e tests via .#e2e-tests (CI canonical, no cargo)
just nix-test-smoke              # Docker smoke test via .#smoke-test (CI canonical)
just rust-test-e2e               # inner-loop Rust e2e via cargo (needs dev shell)
just hurl-test                   # Hurl HTTP contract tests (needs hurl — from dev shell)
just nix-run                     # run sipi with the dev config
just nix-valgrind                # run sipi under Valgrind

# Bazel inner loop (DEV-6343, PR Y+1 — local-only; CI runs unit + approval
# tests via just nix-build's checkPhase until DEV-6348 (Y+6))
just bazel-test-unit             # bazel test //test/unit/...  (12 of 12 components)
just bazel-test-approval         # bazel test //test/approval:approvaltests  (24 cases, env-injected SOURCE_DATE_EPOCH)

# Dev-shell inner loop (non-recipe — see "Inner-loop development" below)

# Docker push / manifest (build is via Nix — see nix-docker-build* above)
just test-smoke                  # build image via Nix, then run smoke tests (inner-loop)
just docker-push-amd64           # push already-loaded amd64 image
just docker-push-arm64           # push already-loaded arm64 image
just docker-publish-manifest     # publish multi-arch manifest

# Vendor dependencies
just vendor-download             # download all dep archives to vendor/
just vendor-verify               # verify SHA-256 checksums
just vendor-checksums            # print checksums for manifest updates
just kakadu-fetch                # download Kakadu archive (dev-shell inner loop only; Nix builds fetch via FOD)

# Documentation
just docs-serve                  # serve docs locally
```

### Inner-loop development (incremental rebuilds)

The justfile does not expose an imperative build recipe — by design. A recipe
is a contract that CI runs the same command, and CI always goes through a Nix
derivation. For the fast edit/rebuild/run cycle on a single `.cpp` file, drop
into the dev shell and call cmake by hand:

```bash
nix develop                                   # dev shell with all build deps
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCODE_COVERAGE=ON
cmake --build build --parallel
./build/sipi --config config/sipi.localdev-config.lua
# subsequent edits:
cmake --build build                           # incremental
```

**Caveats:** this path is non-reproducible and does not match CI outputs
byte-for-byte. To run the reproducible CI build, use `just nix-build` instead.

**Prerequisite:** run `just kakadu-fetch` once per Kakadu version — the
dev-shell cmake invocation consumes `vendor/v8_5-01382N.zip` from that.

## High-Level Architecture

### Core Components

| Component | Path | Purpose |
|-----------|------|---------|
| Main Application | `src/sipi.cpp` | Entry point (CLI + server modes), CLI11 arg parsing, Sentry integration |
| SipiImage | `src/SipiImage.hpp` | Image processing: TIFF, JP2, PNG, JPEG; metadata (EXIF, IPTC, XMP); ICC profiles |
| SipiHttpServer | `src/SipiHttpServer.hpp` | HTTP server, IIIF endpoints, caching, Lua scripting integration |
| IIIF Parser | `include/iiifparser/` | IIIF URL parsing: identifier, region, size, rotation, quality/format |
| Format Handlers | `include/formats/` | SipiIO base class + SipiIOTiff, SipiIOJ2k, SipiIOJpeg, SipiIOPng |
| SHTTPS Framework | `shttps/` | HTTP server impl: threading, SSL/TLS, connection pooling, JWT auth |
| Caching | `include/SipiCache.h` | File-based LRU cache with dual-limit eviction (size + file count), crash recovery |
| Metrics | `include/SipiMetrics.h` | Prometheus metrics singleton — cache counters/gauges exposed at `GET /metrics` |
| Memory Budget | `include/SipiMemoryBudget.h` | Lock-free decode memory budget with RAII guard — prevents OOM from concurrent large decodes |
| Lua Integration | `include/SipiLua.h` | Lua bindings for image manipulation, HTTP handling, config/routes |

### Image Processing Pipeline

1. HTTP server receives IIIF URL
2. IIIF parameters extracted and validated
3. Cache check (SipiCache)
4. Image loaded via appropriate SipiIO handler
5. Processing: region, scaling, rotation, quality
6. Serve processed image or write to cache

### Configuration

- Main config: `config/sipi.config.lua`
- Test config: `config/sipi.test-config.lua`
- Local dev config: `config/sipi.localdev-config.lua`
- Lua scripts: `scripts/` directory

### Dependencies

**External Libraries (built from source in `ext/`):**
Image formats (libtiff, libpng, libjpeg, libwebp), compression (zlib, bzip2, xz, zstd), JPEG2000 (kakadu — requires license), metadata (exiv2, lcms2), Lua + luarocks, jansson, sqlite3, sentry, prometheus-cpp (core only), OpenSSL, libcurl, libmagic.

**System Dependencies:** Threads (pthread), iconv (macOS only).

### Important Files

- `CMakeLists.txt` — main build configuration
- `justfile` — all build targets (run `just` to list)
- `version.txt` — version information
- `flake.nix` — Nix build system (overlay, package variants, dev shells)
- `package.nix` — Nix derivation for Sipi
- `nix/` — Nix expressions imported by `flake.nix`. Each file is a function over `{ pkgs, … }` returning attribute set(s) that `flake.nix` merges into the right output. New derivations go here, not into `flake.nix` — keeping the flake an orchestrator is an explicit goal. Existing in-flake builders (Kakadu FOD, static builds, Docker image, dev shells) are slated for migration into `nix/<topic>.nix` files using this pattern.

## Testing

For the authoritative testing strategy (pyramid, layer definitions, decision tree, IIIF coverage matrix, feature inventory), see [`docs/src/development/testing-strategy.md`](docs/src/development/testing-strategy.md).

For test framework details (how to run tests, directory layout, adding tests), see [`docs/src/development/developing.md`](docs/src/development/developing.md).

- **Unit tests** (`test/unit/`): GoogleTest + ApprovalTests — run inside the `.#dev` / `.#default` derivation's `checkPhase` (`just nix-build` fails if any unit test fails)
- **E2E tests** (`test/e2e-rust/`): Rust (reqwest + cargo test). CI: `just nix-test-e2e` (binaries from `.#e2e-tests`). Inner-loop: `just rust-test-e2e` (cargo from dev shell).
- **Hurl tests** (`test/hurl/`): HTTP contract tests — `just hurl-test`
- **Smoke tests** (`test/e2e-rust/tests/docker_smoke.rs`): against Docker image. CI: `just nix-test-smoke` (binary from `.#smoke-test`). Inner-loop: `just test-smoke`.
- **Approval tests** (`test/approval/`): snapshot-based regression — included in unit tests

Run a specific unit test binary in the dev-shell inner loop: `cd build && test/unit/<component>/<component>`

## CI, Release, and Commit Messages

For CI pipeline details (Docker publishing, release automation), see [`docs/src/development/ci.md`](docs/src/development/ci.md).

**Releases are automated via release-please.** Correct [Conventional Commit](https://www.conventionalcommits.org/) prefixes are required — they drive SemVer bumps and changelog generation. See [`docs/src/development/ci.md`](docs/src/development/ci.md) for the full prefix-to-release mapping and [`docs/src/development/developing.md`](docs/src/development/developing.md) for the commit message schema.

Valid prefixes: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `build`, `chore`, `ci`, `perf`. Breaking changes use `!` suffix: `feat!: ...`

For commit organization rules (how to group commits in PRs) and PR description format (optimized for learnings extraction), see [`docs/src/development/commit-conventions.md`](docs/src/development/commit-conventions.md).

**Code review:** Use [`docs/src/development/reviewer-guidelines.md`](docs/src/development/reviewer-guidelines.md) as the review checklist for all PRs.

## C++ Style Guide

Follow [`docs/src/development/cpp-style-guide.md`](docs/src/development/cpp-style-guide.md) for all new and modified C++ code. Key rules:

- **Ownership:** No raw owning `new`/`delete` — use `std::unique_ptr`, `std::make_unique`, or value semantics
- **Error handling:** `std::expected<T, E>` for fallible operations, exceptions for truly unrecoverable conditions
- **Input validation:** Validate all user input at HTTP handler boundaries before any file I/O or header construction
- **`[[nodiscard]]`:** Apply to all functions where ignoring the return value is a bug
- **const correctness:** Apply `const` everywhere it is valid
- **Legacy code:** When modifying existing code, apply modernization opportunistically (see style guide Section 4)

## Scope discipline

Rules for what to build (not how). Follow unless the user explicitly asks to override.

- **No backwards-compatibility shims.** Update every caller in the same change. Do not leave deprecated aliases, renamed-variable pointers, re-exports, or "keep the old name for now" comments. This repo uses rebase-merge, not squash; history preserves the reasoning.
- **No defense-in-depth.** Validate at system boundaries only — HTTP request handlers, FFI boundaries (C library calls), user-facing CLI parsers. See `REVIEW.md` §"Security (input validation)" for what qualifies. No redundant null checks, double validation, or `try`/`catch` around code that can't fail under the current contract.
- **No enterprise abstractions — KISS.** Prefer three similar lines over one parameterized helper. Prefer a concrete type over a trait/interface with a single implementation. Introduce an abstraction only for a second *real* caller, not a hypothetical one.
- **Ask when in doubt.** If a task is ambiguous, if two reasonable approaches exist, or if a new pattern/file/dep feels load-bearing, surface the decision to the user before acting. "Suggest, don't decide" is the default; autonomy is granted explicitly per-task.

These are not style preferences — they are contract with the maintainer. Code that violates them is code the maintainer did not ask for.

## Development Notes

**Compiler Requirements:** C++23, Clang >= 15.0 or GCC >= 13.0, CMake >= 3.28

**Build Types:** Debug (`-O0 -g`), Release (`-O3 -DNDEBUG`), RelWithDebInfo (`-O3 -g`)

**Error Reporting:** Optional Sentry integration via `SIPI_SENTRY_DSN`, `SIPI_SENTRY_ENVIRONMENT`, `SIPI_SENTRY_RELEASE` environment variables.
