# Fix: Sipi Zig CI failures and review findings (PR #510)

> **Type**: fix
> **Date**: 2026-03-04
> **PR**: [dasch-swiss/sipi#510](https://github.com/dasch-swiss/sipi/pull/510)
> **Linear**: Follow-up items tracked in DEV-5969, Rust rewrite analysis in DEV-5970

## Problem

PR #510 adds Zig toolchain support for static Linux binaries. All 5 CI jobs are failing:

| Job | Error | Root Cause |
|-----|-------|------------|
| `zig-static / amd64` | `ninja: error: 'local/lib/liblcms2.a' missing` | Missing ExternalProject DEPENDS on jpeg/tiff |
| `zig-static / arm64` | Same as above | Same |
| `amd64` (publish) | `Could not find autoreconf` | docker-sipi-base missing autotools |
| `arm64` (publish) | Same as above | Same |
| `zig-macos / arm64` | `_PrintGifError` undefined | webp builds gif2webp without libgif |

A multi-perspective code review also identified issues to fix in this PR.

## Steps

### Step 0: Rebase sipi branch onto main

**Repo**: sipi

Before making any changes, rebase the workspace branch onto `origin/main` to incorporate any upstream changes and reduce merge conflicts later:

```bash
cd sipi/
git fetch origin
git rebase origin/main
```

Resolve any conflicts before proceeding.

### Step 1: Fix lcms2 Ninja build-order failure

**Repo**: sipi
**File**: `ext/lcms2/CMakeLists.txt`

`project_lcms2` configures with `--with-jpeg` and `--with-tiff` but declares no `DEPENDS` on those ExternalProjects. With Make generators this works by accident (subdirectory order), but Ninja only respects explicit `DEPENDS` clauses.

Add `DEPENDS project_jpeg project_tiff`. Use repeated `DEPENDS` keywords — this is valid CMake and matches the style already used by `ext/tiff`, `ext/exiv2`, and `ext/png` in this codebase:

```cmake
ExternalProject_Add(project_lcms2
        DEPENDS project_jpeg
        DEPENDS project_tiff
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        ...
)
```

### Step 2: Fix libmagic missing ExternalProject dependencies

**Repo**: sipi
**File**: `ext/libmagic/CMakeLists.txt`

`magic_static` declares `INTERFACE_LINK_LIBRARIES "bzip2;xz;zlib;zstd"` but `project_libmagic` has no `DEPENDS` on those projects. Its configure may fail to detect zlib/lzma headers.

Add DEPENDS to ExternalProject_Add:

```cmake
ExternalProject_Add(project_libmagic
    DEPENDS project_bzip2 project_xz project_zlib project_zstd
    INSTALL_DIR ${CMAKE_BINARY_DIR}/local
    ...
)
```

### Step 3: Fix webp gif2webp link failure on macOS

**Repo**: sipi
**File**: `ext/webp/CMakeLists.txt`

webp tries to build `gif2webp` which requires `libgif` (not available on macOS CI with Zig). The file already has `-DCMAKE_DISABLE_FIND_PACKAGE_TIFF=TRUE` and `-DWEBP_BUILD_CWEBP=OFF`. Add two new flags to disable GIF support:

```cmake
            -DWEBP_BUILD_GIF2WEBP=OFF
            -DCMAKE_DISABLE_FIND_PACKAGE_GIF=TRUE
```

These go alongside the existing CMAKE_ARGS. No other flags need changing.

### Step 4: Eliminate docker-sipi-base — inline minimal deps into sipi Dockerfile

**Repo**: sipi
**File**: `Dockerfile`

Replace `FROM $SIPI_BASE AS builder` with `FROM ubuntu:24.04 AS builder` and install only the packages actually needed for compilation. Analysis shows ~70% of docker-sipi-base packages are dead weight (e2e test tools, redundant `-dev` libs for things built from source in `ext/`, PPAs for cmake/gcc that Ubuntu 24.04 no longer needs).

**Ubuntu 24.04 ships:**
- cmake 3.28.3 (meets `>=3.28` requirement — no Kitware PPA needed)
- GCC 14 in universe repo (meets `>=13.0` C++23 requirement — no toolchain PPA needed)

**Packages actually needed by the builder stage:**

| Package | Why |
|---------|-----|
| `build-essential` | Compiler toolchain (GCC 14 from default repos) |
| `cmake` | Build system (3.28.3 from default repos) |
| `pkg-config` | Used by ext/ configure scripts |
| `libreadline-dev` | Lua's `make linux` links `-lreadline` |
| `autoconf`, `automake`, `libtool` | ext/libmagic needs `autoreconf` |
| `gettext` | May be needed by ext/ configure |
| `locales` | `locale-gen` for unit tests |
| `curl`, `wget` | Downloading sources |
| `file` | libmagic runtime (for ctest) |
| `git` | Version detection during build |

**Packages NOT needed** (were in docker-sipi-base but unused or redundant):
- `valgrind`, `doxygen` — dev tools not used in Docker build
- `libcurl4-openssl-dev`, `libssl-dev`, `libmagic-dev` — built from source in `ext/`
- `libidn11-dev` — curl built with `--without-libidn2`
- `libnuma-dev`, `libacl1-dev`, `uuid-dev`, `gperf` — not referenced in CMakeLists
- `libtiff5-dev`, `libopenjp2-7-dev`, `libxml2-dev`, `libxslt1-dev` — only for e2e tests
- `nginx`, `graphicsmagick`, `imagemagick`, `apache2-utils` — only for e2e tests
- `python3`, `pytest`, `requests`, `psutil`, `iiif_validator`, `Sphinx` — e2e/docs only
- `software-properties-common` — only needed for PPAs (no longer required)

Replace the builder stage (lines 1-8) with:

```dockerfile
# STAGE 1: Build
FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    libreadline-dev \
    autoconf \
    automake \
    libtool \
    gettext \
    locales \
    curl \
    wget \
    file \
    git \
  && locale-gen en_US.UTF-8 \
  && locale-gen sr_RS.UTF-8 \
  && apt-get clean \
  && rm -rf /var/lib/apt/lists/*

ENV LC_ALL=en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LANGUAGE=en_US.UTF-8
```

Also remove the `ARG SIPI_BASE` and `ARG UBUNTU_BASE` lines at the top, and update `STAGE 3` to use `ubuntu:24.04` directly instead of `$UBUNTU_BASE`.

**Makefile changes** (exact lines):
- Line 10: Remove `SIPI_BASE := daschswiss/sipi-base:2.23.0`
- Line 11: Remove `UBUNTU_BASE := ubuntu:24.04` (no longer needed — hardcoded in Dockerfile)
- Lines 35, 46, 58, 70, 78, 89, 106, 118, 126, 137: Remove all 10 `--build-arg SIPI_BASE=$(SIPI_BASE) \` references
- Lines 36, 47, 59, 71, 79, 90, 107, 119, 127, 138: Remove all 10 `--build-arg UBUNTU_BASE=$(UBUNTU_BASE) \` references
- Lines 157-175: Remove the entire `docker-build-sipi-dev-env` target (depends on deleted `Dockerfile.sipi-dev-env`)

**Also update `docs/src/development/building.md`**: Line 36 references `daschswiss/sipi-base` — update the prose to reflect that the builder stage now uses `ubuntu:24.04` directly.

**Slim down the final (runtime) stage too.** The current final stage installs packages that have no downstream consumer. Replace the two `RUN apt-get install` blocks with a single minimal one:

```dockerfile
FROM ubuntu:24.04 AS final

ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Zurich

RUN apt-get update && apt-get install -y --no-install-recommends \
    tzdata \
    ca-certificates \
    curl \
    openssl \
    locales \
    ffmpeg \
    libmagic1 \
    file \
  && locale-gen en_US.UTF-8 \
  && locale-gen sr_RS.UTF-8 \
  && apt-get clean \
  && rm -rf /var/lib/apt/lists/*
```

**Packages removed from final stage:**

| Removed | Reason |
|---------|--------|
| `imagemagick` | No Lua script or sipi code references it |
| `at` | No reference in any runtime code |
| `bc` | No reference in any runtime code |
| `uuid` | No reference in any runtime code |
| `byobu`, `htop`, `man`, `vim`, `git`, `unzip`, `wget` | Interactive/dev tools — not needed in a production container |
| `gnupg2`, `software-properties-common` | Only needed for adding PPAs (none used in final stage) |

**Packages kept:**

| Kept | Why |
|------|-----|
| `curl` | healthcheck.sh in knora-sipi |
| `openssl` | TLS for sipi's outbound HTTPS |
| `ca-certificates` | TLS certificate trust store |
| `locales` | UTF-8 locales for sipi |
| `ffmpeg` | `ffprobe` used by dsp-ingest's `MovingImageService` |
| `libmagic1` + `file` | MIME type detection (runtime `.mgc` database) |
| `tzdata` | Timezone support |

### Step 4b: Delete Dockerfile.sipi-dev-env

**Repo**: sipi
**File**: `Dockerfile.sipi-dev-env`

Delete this file. It depends on docker-sipi-base and is no longer needed — developers use Nix (`nix develop`) or Zig for local builds.

### Step 5: Fix `SIPI_RUNTIME_SYSTEM_LIBS` duplicate dl/pthread

**Repo**: sipi
**File**: `CMakeLists.txt` (~line 224)

Current code adds `Threads::Threads` + `${CMAKE_DL_LIBS}` and then conditionally adds bare `dl`, `pthread`. These are genuine duplicates:
- `Threads::Threads` (with `THREADS_PREFER_PTHREAD_FLAG ON`) already provides `-lpthread`
- `${CMAKE_DL_LIBS}` already provides `-ldl` on Linux (empty on macOS where dlopen is in libSystem)
- `rt` (POSIX realtime) is a historical artifact — functions like `clock_gettime` moved into glibc proper in glibc 2.17 (Ubuntu 14.04+). Kept for now as a safety net.

`SIPI_RUNTIME_SYSTEM_LIBS` is consumed in 4 places: `CMakeLists.txt:489` (sipi binary), `test/unit/sipiimage:132`, `test/unit/configuration:74`, `test/unit/logger:23`.

Replace:

```cmake
set(SIPI_RUNTIME_SYSTEM_LIBS Threads::Threads ${CMAKE_DL_LIBS})
if (NOT ZIG_STATIC_BUILD)
    list(APPEND SIPI_RUNTIME_SYSTEM_LIBS dl pthread)
    if (NOT CMAKE_SYSTEM_NAME MATCHES "Darwin")
        list(APPEND SIPI_RUNTIME_SYSTEM_LIBS rt)
    endif ()
endif ()
```

With:

```cmake
set(SIPI_RUNTIME_SYSTEM_LIBS Threads::Threads ${CMAKE_DL_LIBS})
if (NOT ZIG_STATIC_BUILD AND NOT CMAKE_SYSTEM_NAME MATCHES "Darwin")
    list(APPEND SIPI_RUNTIME_SYSTEM_LIBS rt)
endif ()
```

### Step 6: Extract `make_parse_error()` helper in iiif_handler.cpp

**Repo**: sipi
**File**: `src/handlers/iiif_handler.cpp`

`std::expected<IIIFUriParseResult, std::string>(std::unexpect, ...)` is repeated 12 times. Add a helper between lines 24 (`vector_to_string` closing brace) and 41 (the parse function):

```cpp
static auto make_parse_error(std::string msg) -> std::expected<IIIFUriParseResult, std::string>
{
  return std::expected<IIIFUriParseResult, std::string>(std::unexpect, std::move(msg));
}
```

**12 occurrences to replace:**

9 use string literals (lines 71, 138, 162, 182, 192, 214, 260, 283, 293):
```cpp
// Before:
return std::expected<IIIFUriParseResult, std::string>(std::unexpect, "No parameters/path given");
// After:
return make_parse_error("No parameters/path given");
```

3 use dynamic `errmsg.str()` from stringstream (lines 201, 233, 276):
```cpp
// Before:
return std::expected<IIIFUriParseResult, std::string>(std::unexpect, errmsg.str());
// After:
return make_parse_error(errmsg.str());
```

Note: Some error messages have inconsistent trailing `!` (e.g., "Invalid IIIF size!" vs "Invalid IIIF rotation"). Normalize during replacement — remove trailing `!` from all messages for consistency.

### Step 7: Remove Rust feasibility doc

**Repo**: sipi
**File**: `docs/analysis/sipi-rust-rewrite-feasibility.md`

Delete this file. Content has been moved to Linear issue DEV-5970. Also remove the `docs/analysis/` directory if empty afterward.

### Step 8: Add downstream dependencies documentation

**Repo**: sipi
**File**: `docs/src/development/downstream-dependencies.md` (new)

Document which runtime packages the sipi Docker image (final stage) provides and why, so downstream consumers don't need to rediscover this. The doc should cover:

**Sipi's own bundled Lua scripts** (`sipi.config.lua`, `sipi.init.lua`, `test_functions.lua`, `send_response.lua`) have **no system tool dependencies**. They use only sipi's built-in Lua API (`server.http()`, `server.decode_jwt()`, `server.parse_mimetype()`, etc.) — no `io.popen()` or `os.execute()` calls.

**Runtime image (`daschswiss/sipi`) — final stage packages (after Step 4 cleanup):**

| Package | Required by | How it's used |
|---------|------------|---------------|
| `curl` | `knora-sipi` healthcheck | `healthcheck.sh`: `curl -sS --fail 'http://localhost:1024/...'` |
| `openssl` | sipi binary | TLS for outbound HTTPS connections |
| `ca-certificates` | sipi binary | TLS certificate trust store for HTTPS |
| `locales` | sipi binary | UTF-8 locale (`en_US.UTF-8`, `sr_RS.UTF-8`) for string handling |
| `ffmpeg` | dsp-ingest (`MovingImageService`) | `ffprobe` for video metadata (dimensions, duration, FPS). dsp-ingest runs `docker run --entrypoint ffprobe daschswiss/knora-sipi:...` in local dev, or calls `ffprobe` directly in production. |
| `libmagic1` + `file` | sipi binary | MIME type detection (linked at compile time; runtime `.mgc` database needed) |
| `tzdata` | system | Timezone support (`TZ=Europe/Zurich`) |
| `sha256sum` (coreutils) | `knora-sipi` Lua scripts | `util.lua:file_checksum()` calls `/usr/bin/sha256sum` |

**Packages removed (no downstream consumer found):**
`imagemagick`, `at`, `bc`, `uuid`, `byobu`, `htop`, `man`, `vim`, `git`, `unzip`, `wget`, `gnupg2`, `software-properties-common`.

**Downstream consumers:**

| Consumer | Image | What it uses from sipi container |
|----------|-------|----------------------------------|
| `knora-sipi` (dsp-api `sipi/` subproject) | `daschswiss/knora-sipi` (base: `daschswiss/sipi`) | Lua scripts + sipi HTTP server. Needs `curl`, `sha256sum`, `libmagic1`, `locales`, `openssl`, `ca-certificates`. |
| dsp-ingest (`SipiClientLive`) | `daschswiss/knora-sipi` (via `docker run` in local dev) | sipi CLI (`--query`, `--format`, `--topleft`). Needs the sipi binary. |
| dsp-ingest (`MovingImageService`) | `daschswiss/knora-sipi` (via `docker run --entrypoint ffprobe` in local dev) | `ffprobe` for video metadata extraction. Needs `ffmpeg` package. |
| dsp-tools | `daschswiss/knora-sipi` (via Docker Compose) | HTTP API only (port 1024). No direct tool dependencies on container internals. |
| fileidentification | none | No dependency on sipi. Standalone tool with its own ffmpeg/imagemagick/libreoffice. |

Also add this page to the mkdocs nav in `docs/mkdocs.yml`, after the "Developing" entry (line 19):

```yaml
    - Downstream Dependencies: development/downstream-dependencies.md
```

### Step 9: Clean up commit messages to follow Conventional Commits

**Repo**: sipi

The branch has 6 commits since `origin/main`. One commit (`edb2521`) lacks a conventional commit prefix, which breaks release-please changelog generation and SemVer bumping. Another (`46176ee docs: add SIPI Rust rewrite feasibility analysis`) will be obsoleted by Step 7 (file deletion).

Interactive rebase to fix commit messages and squash/reorder as needed:

```
edb2521 Harden Zig static pipeline and document CI workflow  → ci: harden Zig static pipeline and document CI workflow
46176ee docs: add SIPI Rust rewrite feasibility analysis      → squash into parent or drop (file deleted in Step 7)
```

This must happen **after** all other steps are complete, as a final rebase before PR.

### Step 10: Update CLAUDE.md

**Repo**: sipi
**File**: `CLAUDE.md`

- Add release-please and Conventional Commits section explaining that semver is automated via release-please and depends on correct commit prefixes
- Reference existing docs pages (`development/building.md`, `development/ci.md`, `development/developing.md`) instead of duplicating content, to keep CLAUDE.md DRY
- Clean up any redundant sections that duplicate the docs

## Execution Order

1. **Step 0**: Rebase sipi branch onto `origin/main`.
2. **Commit 1 — `fix: CMake build-order deps for Ninja`**: Steps 1, 2, 3, 5 (CMake fixes)
3. **Commit 2 — `fix: eliminate docker-sipi-base, slim runtime image`**: Steps 4, 4b (Dockerfile + Makefile + building.md)
4. **Commit 3 — `refactor: extract make_parse_error() helper`**: Step 6
5. **Commit 4 — `docs: add downstream deps, remove Rust feasibility doc, update CLAUDE.md`**: Steps 7, 8, 10
6. **Step 9**: Interactive rebase to clean up all commit messages on the branch.

Splitting into 4 commits makes it easier to bisect if CI breaks and keeps each commit focused on one concern. The commit message cleanup (Step 9) happens last as a final polish before PR.

docker-sipi-base is no longer modified — it becomes unused after commit 2 inlines the deps.

## Repos Affected

| Repo | Branch | Steps |
|------|--------|-------|
| sipi | `loom-ws/glad-raven-flame` | 0, 1, 2, 3, 4, 4b, 5, 6, 7, 8 |

## Verification

- All 5 failing CI jobs should turn green after these changes
- `gcc / ubuntu-24.04` and `gcc / ubuntu-24.04-arm` (currently pending) should remain passing
- `docs test run` (currently passing) should remain passing
- Docker build no longer depends on `daschswiss/sipi-base` image
