# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SIPI (Simple Image Presentation Interface) is a multithreaded, high-performance, IIIF-compatible media server written in C++23. It implements IIIF Image API 3.0 and provides efficient image format conversions while preserving metadata. The server can be used both as a command-line tool and as a web server with Lua scripting support.

## Build System and Common Commands

All targets are in a single `Makefile`. Run `make help` for a complete list.

### Docker-based Development (Recommended)
```bash
make docker-build       # build Docker image (compiles + runs unit tests)
make test-smoke         # run smoke tests against Docker image
make docker-test-build-amd64   # build + test for amd64 (CI)
make docker-test-build-arm64   # build + test for arm64 (CI)
```

### Native Development (with Nix)

All `nix-*` targets must be run inside a Nix development shell:

```bash
nix develop             # GCC environment (default, used by CI)
nix develop .#clang     # Clang environment (alternative)
```

```bash
make nix-build          # build SIPI (debug + coverage)
make nix-test           # run unit tests
make nix-test-e2e       # run end-to-end tests
make nix-coverage       # generate XML coverage report (gcovr)
make nix-coverage-html  # generate HTML coverage report (lcov)
make nix-run            # start SIPI server
make nix-valgrind       # run with Valgrind
make nix-doxygen-serve  # serve doxygen API docs
```

### Documentation
```bash
make docs-build         # build documentation site
make docs-serve         # serve documentation locally
```

## High-Level Architecture

### Core Components

**Main Application (`src/sipi.cpp`)**
- Entry point supporting both command-line and server modes
- Uses CLI11 for argument parsing
- Integrates with Sentry for error reporting
- Handles global library initialization

**SipiImage (`src/SipiImage.hpp`)**
- Primary image processing class handling all image operations
- Supports TIFF, JPEG2000, PNG, JPEG formats
- Manages pixel data, metadata (EXIF, IPTC, XMP), and ICC color profiles
- Provides scaling, rotation, cropping, color space conversion

**SipiHttpServer (`src/SipiHttpServer.hpp`)**
- Main HTTP server extending shttps::Server framework
- Manages IIIF API endpoints and image serving
- Handles caching, quality settings, and compression
- Integrates with Lua scripting for custom request handling

**IIIF Implementation (`include/iiifparser/`)**
- Complete IIIF 2.0 URL parsing with components:
  - `SipiIdentifier.h` - Image identifier handling
  - `SipiRegion.h` - Region of interest parsing
  - `SipiSize.h` - Size parameter handling
  - `SipiRotation.h` - Rotation parameter parsing
  - `SipiQualityFormat.h` - Quality and format parsing

**Format Handlers (`include/formats/`)**
- Abstract base class SipiIO for image I/O operations
- Concrete implementations: SipiIOTiff, SipiIOJ2k, SipiIOJpeg, SipiIOPng
- Handles reading/writing with region selection and scaling

**SHTTPS Framework (`shttps/`)**
- Custom HTTP server implementation with multi-threading
- SSL/TLS support, connection pooling, keep-alive
- Lua script integration for custom routes
- JWT authentication support

**Caching System (`include/SipiCache.h`)**
- Intelligent file-based caching with size-based limits
- LRU eviction policies and concurrent access protection
- Canonical URL-based cache keys

**Lua Integration (`include/SipiLua.h`)**
- Lua bindings for SipiImage manipulation
- Custom functions for image processing, database access, HTTP handling
- Configuration and route handlers via Lua scripts

### Image Processing Pipeline

1. **Request Reception**: HTTP server receives IIIF URL
2. **URL Parsing**: IIIF parameters extracted and validated
3. **Cache Check**: SipiCache checks for existing processed image
4. **Image Loading**: SipiImage loads original via appropriate SipiIO handler
5. **Processing**: Apply region selection, scaling, rotation, quality adjustments
6. **Output**: Serve processed image or write to cache

### Configuration

- Main config: `config/sipi.config.lua`
- Test config: `config/sipi.test-config.lua`
- Debug config: `config/sipi.debug-config.lua`
- Lua scripts in `scripts/` directory for custom functionality

### Testing Structure

**Unit Tests (`test/unit/`)** — `make nix-test`
- GoogleTest framework with ApprovalTests
- Tests for configuration, iiifparser, sipiimage, logger, handlers
- Run specific test: `cd build && test/unit/[component]/[component]`

**End-to-End Tests (`test/e2e/`)** — `make nix-test-e2e`
- Python pytest-based tests
- Requires a native build (not Docker)

**Approval Tests (`test/approval/`)** — included in `make nix-test`
- Snapshot-based testing for regression detection

**Smoke Tests (`test/smoke/`)** — `make test-smoke`
- Tests against Docker images
- Requires a built Docker image (`make docker-build`)

### Dependencies

**External Libraries (built from source in `ext/`)**
- Image formats: libtiff, libpng, libjpeg, libwebp
- Compression: zlib, bzip2, xz, zstd
- JPEG2000: kakadu (requires license)
- Metadata: exiv2, lcms2 (color profiles)
- Lua: lua + luarocks
- JSON: jansson
- Database: sqlite3
- Error reporting: sentry

**System Dependencies**
- OpenSSL, libcurl, libmagic (via pkg-config)
- Threads (pthread)
- iconv (macOS)

### Important Files

- `CMakeLists.txt` - Main build configuration
- `Makefile` - All build targets (Docker, Nix, docs, CI)
- `version.txt` - Version information
- `vars.mk` - Build variables (Docker repo, build tag)
- `flake.nix` - Nix development environment

### Development Notes

**Compiler Requirements**
- C++23 standard required
- Clang ≥ 15.0 or GCC ≥ 13.0
- CMake ≥ 3.28

**Build Types**
- Debug: `-O0 -g` with debug symbols
- Release: `-O3 -DNDEBUG` optimized
- RelWithDebInfo: `-O3 -g` optimized with debug symbols

**Commit Message Format**
Uses Conventional Commits with prefixes: `feat:`, `fix:`, `docs:`, `style:`, `refactor:`, `test:`, `chore:`

**Error Reporting**
Optional Sentry integration via environment variables:
- `SIPI_SENTRY_DSN` - Sentry project DSN
- `SIPI_SENTRY_ENVIRONMENT` - Environment name
- `SIPI_SENTRY_RELEASE` - Release version