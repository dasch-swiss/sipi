# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SIPI (Simple Image Presentation Interface) is a multithreaded, high-performance, IIIF-compatible media server written in C++23. It implements IIIF Image API 3.0 and provides efficient image format conversions while preserving metadata. The server can be used both as a command-line tool and as a web server with Lua scripting support.

## Build System and Common Commands

All targets are in a single `justfile`. Run `just` for a complete list.
For full build instructions (Docker, Zig, Nix, macOS), see [`docs/src/development/building.md`](docs/src/development/building.md).

**First-time Nix setup:** run `just kakadu-fetch` once to download the proprietary Kakadu archive from `dsp-ci-assets` into `vendor/`. Requires `gh auth login` and `dasch-swiss` org membership. See [`docs/src/development/kakadu.md`](docs/src/development/kakadu.md).

### Quick Reference

```bash
# Docker (recommended for CI-like builds)
just docker-build              # build image (compiles + unit tests)
just test-smoke                # smoke tests against Docker image

# Nix (reproducible native dev — run inside `nix develop`)
just nix-build                 # build (debug + coverage)
just nix-test                  # unit tests
just rust-test-e2e             # Rust e2e tests (requires built sipi)
just hurl-test                 # Hurl HTTP contract tests

# Nix full builds (no shell required; run `just kakadu-fetch` once first)
just nix-build-release         # build release binary via nix
just nix-build-static-amd64    # build static amd64 binary
just nix-build-static-arm64    # build static arm64 binary
just nix-docker-build          # build Docker image via Nix

# Vendor dependencies
just vendor-download           # download all dep archives to vendor/
just vendor-verify             # verify SHA-256 checksums
just vendor-checksums          # print checksums for manifest updates
just kakadu-fetch              # download Kakadu archive (one-time per version)

# Documentation
just docs-serve                # serve docs locally
```

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

## Testing

For the authoritative testing strategy (pyramid, layer definitions, decision tree, IIIF coverage matrix, feature inventory), see [`docs/src/development/testing-strategy.md`](docs/src/development/testing-strategy.md).

For test framework details (how to run tests, directory layout, adding tests), see [`docs/src/development/developing.md`](docs/src/development/developing.md).

- **Unit tests** (`test/unit/`): GoogleTest + ApprovalTests — `just nix-test`
- **E2E tests** (`test/e2e-rust/`): Rust (reqwest + cargo test) — `just rust-test-e2e`
- **Hurl tests** (`test/hurl/`): HTTP contract tests — `just hurl-test`
- **Smoke tests** (`test/smoke/`): against Docker image — `just test-smoke`
- **Approval tests** (`test/approval/`): snapshot-based regression — included in unit tests

Run a specific test binary: `cd build && test/unit/<component>/<component>`

## CI, Release, and Commit Messages

For CI pipeline details (Zig validation gates, static artifact flow, Docker publishing), see [`docs/src/development/ci.md`](docs/src/development/ci.md).

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
