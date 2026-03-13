# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SIPI (Simple Image Presentation Interface) is a multithreaded, high-performance, IIIF-compatible media server written in C++23. It implements IIIF Image API 3.0 and provides efficient image format conversions while preserving metadata. The server can be used both as a command-line tool and as a web server with Lua scripting support.

## Build System and Common Commands

All targets are in a single `Makefile`. Run `make help` for a complete list.
For full build instructions (Docker, Zig, Nix, macOS), see [`docs/src/development/building.md`](docs/src/development/building.md).

### Quick Reference

```bash
# Docker (recommended for CI-like builds)
make docker-build              # build image (compiles + unit tests)
make test-smoke                # smoke tests against Docker image

# Zig (local dev, no Nix required)
make zig-build-local           # build with Zig toolchain
make zig-test                  # unit tests
make zig-test-e2e              # end-to-end tests

# Nix (reproducible native dev — run inside `nix develop`)
make nix-build                 # build (debug + coverage)
make nix-test                  # unit tests
make rust-test-e2e             # Rust e2e tests (requires built sipi)
make hurl-test                 # Hurl HTTP contract tests

# Vendor dependencies
make vendor-download           # download all dep archives to vendor/
make vendor-verify             # verify SHA-256 checksums
make vendor-checksums          # print checksums for manifest updates

# Documentation
make docs-serve                # serve docs locally
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
- `Makefile` — all build targets (run `make help`)
- `version.txt` — version information
- `vars.mk` — Docker repo/tag variables
- `flake.nix` — Nix development environment

## Testing

For the authoritative testing strategy (pyramid, layer definitions, decision tree, IIIF coverage matrix, feature inventory), see [`docs/src/development/testing-strategy.md`](docs/src/development/testing-strategy.md).

For test framework details (how to run tests, directory layout, adding tests), see [`docs/src/development/developing.md`](docs/src/development/developing.md).

- **Unit tests** (`test/unit/`): GoogleTest + ApprovalTests — `make nix-test` or `make zig-test`
- **E2E tests** (`test/e2e-rust/`): Rust (reqwest + cargo test) — `make rust-test-e2e` or `make zig-test-e2e`
- **Hurl tests** (`test/hurl/`): HTTP contract tests — `make hurl-test`
- **Smoke tests** (`test/smoke/`): against Docker image — `make test-smoke`
- **Approval tests** (`test/approval/`): snapshot-based regression — included in unit tests

Run a specific test binary: `cd build && test/unit/<component>/<component>`

## CI, Release, and Commit Messages

For CI pipeline details (Zig validation gates, static artifact flow, Docker publishing), see [`docs/src/development/ci.md`](docs/src/development/ci.md).

**Releases are automated via release-please.** Correct [Conventional Commit](https://www.conventionalcommits.org/) prefixes are required — they drive SemVer bumps and changelog generation. See [`docs/src/development/ci.md`](docs/src/development/ci.md) for the full prefix-to-release mapping and [`docs/src/development/developing.md`](docs/src/development/developing.md) for the commit message schema.

Valid prefixes: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `build`, `chore`, `ci`, `perf`. Breaking changes use `!` suffix: `feat!: ...`

For commit organization rules (how to group commits in PRs) and PR description format (optimized for learnings extraction), see [`docs/src/development/commit-conventions.md`](docs/src/development/commit-conventions.md).

**Code review:** Use [`docs/src/development/reviewer-guidelines.md`](docs/src/development/reviewer-guidelines.md) as the review checklist for all PRs.

## Development Notes

**Compiler Requirements:** C++23, Clang >= 15.0 or GCC >= 13.0, CMake >= 3.28

**Build Types:** Debug (`-O0 -g`), Release (`-O3 -DNDEBUG`), RelWithDebInfo (`-O3 -g`)

**Error Reporting:** Optional Sentry integration via `SIPI_SENTRY_DSN`, `SIPI_SENTRY_ENVIRONMENT`, `SIPI_SENTRY_RELEASE` environment variables.
