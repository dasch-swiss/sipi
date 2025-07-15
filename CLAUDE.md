# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SIPI (Simple Image Presentation Interface) is a multithreaded, high-performance, IIIF-compatible media server written in C++23. It implements IIIF Image API 3.0 and provides efficient image format conversions while preserving metadata. The server can be used both as a command-line tool and as a web server with Lua scripting support.

## Build System and Common Commands

### Docker-based Development (Recommended)
```bash
# Compile SIPI with debug symbols
make compile

# Run tests
make test

# Build and run server
make run

# Build Docker image
make docker-build

# Run smoke tests
make test-smoke
```

### Native Development (with Nix)
```bash
# Enter Nix development shell
nix develop .#clang    # or `just clang`
nix develop           # gcc environment, or `just gcc`

# Build project
just build            # or cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build ./build --parallel

# Run tests
just test             # or cd build && ctest --output-on-failure

# Generate test coverage
just coverage         # uses gcovr
just coverage1        # uses lcov with HTML output

# Run SIPI server
just run              # or ./build/sipi --config=config/sipi.config.lua

# Run with Valgrind
just valgrind
```

### macOS Development (Not Recommended)
```bash
mkdir -p ./build-mac && cd build-mac && cmake .. && make && ctest --verbose
```

### Documentation
```bash
# Build documentation
make docs-build

# Serve documentation locally
make docs-serve

# Serve doxygen docs (after building via cmake)
just doxygen-serve
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

**Unit Tests (`test/unit/`)**
- GoogleTest framework with ApprovalTests
- Tests for configuration, iiifparser, sipiimage, logger, handlers
- Run specific test: `cd build && test/unit/[component]/[component]`

**End-to-End Tests (`test/e2e/`)**
- Python pytest-based tests
- Run with: `pytest -s --sipi-exec=./build/sipi` (from test/e2e/)

**Approval Tests (`test/approval/`)**
- Snapshot-based testing for regression detection

**Smoke Tests (`test/smoke/`)**
- Tests against Docker images
- Run with: `pytest -s test/smoke`

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
- `Makefile` - Docker-based build targets
- `justfile` - Nix-based development commands
- `version.txt` - Version information
- `vars.mk` - Build variables
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