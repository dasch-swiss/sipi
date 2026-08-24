---
title: "Sipi Nix Unified Build"
date: 2026-04-16
author: "Ivan Subotic"
status: superseded
superseded_by: "specs/2026-04-29-sipi-bazel-migration/01-refactor-sipi-bazel-migration-plan.md"
linear: DEV-6265
repositories:
  - sipi
  - dsp-ci-assets
---

> **Superseded.** Phase 1 of this plan landed in production (the
> previous frontmatter status was `implemented`). The follow-on phases
> — and the Nix-as-build-orchestrator architecture itself — are
> obsoleted by [the Sipi Bazel migration](../2026-04-29-sipi-bazel-migration/01-refactor-sipi-bazel-migration-plan.md)
> (DEV-6341). After PR Y+7 (DEV-6349) of that migration, Nix's role
> contracts to dev-shell provisioning only and Bazel is the single
> build system.

# Sipi Nix Unified Build — Implementation Plan

## Executive Summary

Migrate Sipi's build system to a Nix-centric architecture where Nix is the
single entry point for all build variants: local development, CI testing,
Docker image production, static binary releases, and debug symbol extraction.
Phase 1 wraps the existing CMake + ExternalProject build inside Nix overlays
without rewriting the dependency management. This preserves the battle-tested
`ext/` build-from-source approach while gaining Nix's reproducibility,
caching, and composability.

---

## 1. Current State Analysis

### 1.1 Build Toolchains (Three Parallel Worlds)

| Toolchain | Where Used | Compiler | Stdlib | Static? | Dependencies |
|-----------|-----------|----------|--------|---------|-------------|
| **Docker/Clang** | CI Docker builds, production images | Clang (Ubuntu 24.04) | libc++ | No (glibc dynamic, deps static) | `ext/` ExternalProject from source |
| **Nix/Clang** | Local dev, CI unit+e2e tests | Clang 19 via `llvmPackages_19.libcxxStdenv` | libc++ | No | `ext/` ExternalProject from source |
| **Zig** | Static binaries, macOS dylib audit | Zig cc (Clang-based) | libc++ (bundled) | Yes (musl) | `ext/` ExternalProject from source |

**Key insight:** All three toolchains share the same CMake build system and the
same `ext/` ExternalProject dependency tree. The only differences are the
compiler driver and the libc target (glibc vs musl).

### 1.2 Existing Nix Infrastructure

**`flake.nix`** (136 lines): Provides dev shells (`clang`, `fuzz`, `gcc`) but
the shell package list is manually maintained — it duplicates `package.nix`
inputs and drifts. The `TODO` comment on line 18 acknowledges this.

**`package.nix`** (61 lines): Declares a Nix derivation but has several issues:
- `src = ./.;` — no source filtering, so any file change (docs, CI configs)
  invalidates the entire build cache
- Version hardcoded (`3.8.12`) instead of read from `version.txt`
- `buildInputs` lists packages that are only used for their CLI tools (curl,
  openssl, file) — the actual libraries are built from source by `ext/`
- `makeFlags = [ "-j 1" ]` — single-threaded build, presumably because the
  full `nix build` path was never production-ready
- No overlay export, no Docker image output, no static build variant

### 1.3 CI Workflows

| Workflow | Runner | Build Method | Caching |
|----------|--------|-------------|---------|
| `test.yml` (nix-clang) | ubuntu-24.04{,-arm} | `nix develop` + `make` targets | **None** — no Cachix, no cache-nix-action |
| `test.yml` (zig-static) | ubuntu-24.04{,-arm} | Alpine Docker + Zig | **None** — builds from scratch every run |
| `test.yml` (zig-macos) | macos-14 | Zig native | **None** |
| `docker-build.yml` | ubuntu-24.04{,-arm} | `docker buildx` | GHA cache (layer cache) |
| `publish.yml` (Docker) | ubuntu-24.04{,-arm} | `docker buildx` | GHA cache (layer cache) |
| `publish.yml` (static) | ubuntu-24.04{,-arm} | Alpine Docker + Zig | **None** |

**Critical gap:** The Nix and Zig CI paths have **zero caching**. Every PR
rebuild compiles 20+ external dependencies from source (~15-25 min). Only the
Docker path benefits from layer caching.

### 1.4 Release Artifacts (Current)

| Artifact | Method | Published To |
|----------|--------|-------------|
| Docker image (amd64+arm64) | `docker buildx` multi-stage | Docker Hub `daschswiss/sipi:vX.Y.Z` |
| Static binary (amd64) | Zig + Alpine | GitHub Release `.tar.gz` + `.sha256` |
| Static binary (arm64) | Zig + Alpine | GitHub Release `.tar.gz` + `.sha256` |
| Debug symbols (Docker) | `objcopy --only-keep-debug` | Sentry |
| Debug symbols (static) | `objcopy --only-keep-debug` | Sentry + GitHub Release |
| SBOM | Docker Scout | Artifact (per-arch) |

### 1.5 Dependency Management

All 20+ dependencies are declared in `cmake/dependencies.cmake` with version,
URL, SHA-256, and filename. Archives are vendored in `vendor/` (Git LFS) for
offline builds. `ext/*/CMakeLists.txt` uses `ExternalProject_Add()` to build
each from source with precise configure flags and dependency ordering.

**This is the part we explicitly preserve in Phase 1.** The `ext/` build
system is battle-tested, handles Kakadu (proprietary), and ensures consistent
static linking across all toolchains.

---

## 2. Target Architecture

### 2.1 Design Principles

1. **Nix as the single entry point (target state)** — `nix build .#<target>`
   for every artifact (dev binary, Docker image, static binary, debug symbols).
   Phase 1 achieves this for dev and static builds; Docker transitions later
2. **Wrap, don't rewrite** — Phase 1 keeps `ext/` ExternalProject. Phase 2
   (future) may migrate individual deps to nixpkgs where beneficial
3. **Cache everything** — Binary cache (Cachix) for CI, local Nix store for
   dev machines
4. **One derivation, many variants** — `package.nix` parameterized by stdenv,
   build type, and feature flags; overlay makes it composable
5. **Source filtering** — Only build-relevant files trigger rebuilds
6. **Static tarball as the distribution unit** — The primary release artifact
   is a self-contained static binary tarball (amd64 + arm64), not a Docker
   image. Downstream consumers (dsp-api) build their own Docker images from
   the tarball. This decouples Sipi's release from Docker Hub and gives
   consumers full control over their image composition

### 2.2 Flake Output Map

```
packages.${system} = {
  default         # RelWithDebInfo, Clang/libc++, separateDebugInfo
                  #   .debug output has symbols (nix build .#default.debug)
  dev             # Debug, unstripped, coverage-instrumented
  release         # Release, unstripped (for manual distribution)

  # Static Linux binaries (only on Linux)
  static-amd64    # x86_64-linux-musl, Zig toolchain, Release
  static-arm64    # aarch64-linux-musl, Zig toolchain, Release

  # Release archives (tarball + checksum + debug symbols)
  release-archive-amd64  # .tar.gz + .sha256 + .debug for amd64
  release-archive-arm64  # .tar.gz + .sha256 + .debug for arm64

  # Docker images (only on Linux)
  docker          # Minimal OCI image from nix dockerTools
  docker-stream   # Streaming variant for CI (no store write)
};

devShells.${system} = {
  default         # = clang
  clang           # llvmPackages_19.libcxxStdenv + dev tools
  fuzz            # llvmPackages_19.stdenv (libstdc++ for libFuzzer ABI)
  gcc             # gcc14Stdenv
};

overlays.default   # final: prev: { sipi = ...; }
```

### 2.3 Caching Strategy

| Layer | Mechanism | Scope | Cost |
|-------|-----------|-------|------|
| **Local dev** | Nix store (`/nix/store`) | Per-machine | Free |
| **CI (primary)** | Cachix binary cache | Cross-workflow, cross-branch | Free tier (5GB) or paid |
| **CI (fallback)** | `nix-community/cache-nix-action` | Per-repo, 10GB GHA limit | Free |
| **Docker layers** | GHA cache (existing) | Per-repo | Free |

**Cachix flow:**
1. CI runs `nix build .#default`
2. New store paths automatically pushed to Cachix
3. Next CI run substitutes cached paths — skips entire `ext/` compilation
4. Local devs add Cachix substituter to get CI-built dependencies

**Expected impact:** PR CI time drops from ~20 min to ~3-5 min (cache hit on
dependencies, only recompile changed Sipi source).

---

## 3. Implementation Sections

### Section 0: Fix RelWithDebInfo Compiler Flags (Prerequisite)

**Files:** `CMakeLists.txt`

**Problem:** The production build flags at lines 85-86 are:
```cmake
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -g -DNDEBUG" CACHE STRING "..." FORCE)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -DNDEBUG" CACHE STRING "..." FORCE)
```

At `-O3`, both GCC and Clang default to `-fomit-frame-pointer` on x86_64 and
aarch64. This means the production binary (Docker and static release builds)
has **no frame pointers**. `backtrace()`, Sentry's stack unwinder, and `perf`
will all produce incomplete or broken stacks. This undermines the entire debug
symbol pipeline — Sentry receives the symbols but often can't walk the stack
far enough for them to matter.

`-fno-omit-frame-pointer` currently only appears inside the `ENABLE_SANITIZERS`
block (line 247), which is off in production.

**Fix:**
```cmake
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -g -DNDEBUG -fno-omit-frame-pointer" CACHE STRING "C flags for RelWithDebInfo" FORCE)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -DNDEBUG -fno-omit-frame-pointer" CACHE STRING "C++ flags for RelWithDebInfo" FORCE)
```

**Performance cost:** Negligible on modern x86_64/aarch64 — one general-purpose
register (RBP/x29) is reserved for the frame chain instead of being available
to the register allocator. Benchmarks on similar workloads show < 1% regression.

**Impact:** All three build toolchains (Docker, Nix, Zig) share the same
`CMakeLists.txt`, so this fix immediately improves stack traces in every
production artifact.

**Acceptance criteria:**
- `RelWithDebInfo` builds include `-fno-omit-frame-pointer` in compiler flags
- `readelf --debug-dump=frame` confirms `.eh_frame` has complete unwind info
- Existing unit, e2e, and hurl tests pass (no behavioral change)
- This should be a standalone commit/PR merged before the Nix work begins

---

### Section 1: Fix `package.nix` — Make It Production-Ready

**Files:** `package.nix`

**Changes:**

1. **Source filtering** with `lib.fileset.toSource`:
   ```nix
   src = lib.fileset.toSource {
     root = ./.;
     fileset = lib.fileset.unions [
       ./CMakeLists.txt
       ./version.txt
       ./generate_icc_header.sh
       ./cmake
       ./ext
       ./vendor
       ./include
       ./src
       ./shttps
       ./fuzz
       ./patches
       ./test
       ./config
       ./scripts
       ./server
     ];
   };
   ```
   This excludes `.github/`, `docs/`, `CLAUDE.md`, `.gitignore`, etc. from
   the build input hash, so doc-only changes don't trigger rebuilds.

2. **Dynamic version** from `version.txt`:
   ```nix
   version = lib.strings.trim (builtins.readFile ./version.txt);
   ```

3. **Correct dependency classification:**
   - `nativeBuildInputs`: cmake, pkg-config, autoconf, automake, libtool,
     gettext, m4, unzip, xxd, file (for autoreconf)
   - `buildInputs`: readline, iconv, gperf, libuuid, perl (build-time
     deps that the CMake build actually links or uses)
   - Remove packages that `ext/` builds from source (curl, openssl, ffmpeg
     are NOT needed as Nix buildInputs — they're compiled by ExternalProject)

4. **Parameterize build type and features:**
   ```nix
   { lib, stdenv, cmake, ...,
     cmakeBuildType ? "RelWithDebInfo",
     enableCoverage ? false,
     enableSanitizers ? false,
     enableTests ? true,
     providedVersion ? null }:
   ```

5. **Parallel builds:** Remove `makeFlags = [ "-j 1" ]`, use Nix's default
   parallelism (respects `--cores` / `NIX_BUILD_CORES`)

6. **`separateDebugInfo = true`** for the default (non-static) package — Nix
   automatically produces a `debug` output with `.build-id` indexed symbols.
   Note: static builds use manual `objcopy` instead (see Section 4) because
   `separateDebugInfo` can conflict with static binaries

7. **Environment variables** for Clang/libc++ consistency:
   ```nix
   env = {
     CXXFLAGS = "-stdlib=libc++";
     LDFLAGS = "-stdlib=libc++ -Wno-unused-command-line-argument";
   };
   ```

**Acceptance criteria:**
- `nix build .#default` produces a working `sipi` binary
- `nix build .#default.debug` produces debug symbols
- Changing `CLAUDE.md` does not trigger a rebuild (source filtering works)
- Build uses all available cores (no `-j 1`)
- Version reported by `sipi --version` matches content of `version.txt`
- `nix show-derivation .#default` confirms curl, openssl, ffmpeg are NOT in
  `buildInputs` (they are built by `ext/`)
- Compiler flags include `-stdlib=libc++`

---

### Section 2: Restructure `flake.nix` — Overlay + Multiple Outputs

**Files:** `flake.nix`

**Changes:**

1. **Export an overlay:**
   ```nix
   overlays.default = final: prev: {
     sipi = prev.callPackage ./package.nix { };
   };
   ```

2. **Apply the overlay to pkgs:**
   ```nix
   pkgs = import nixpkgs {
     inherit system;
     overlays = [ self.overlays.default ];
   };
   ```

3. **Define package variants:**
   ```nix
   packages = {
     default = pkgs.sipi;

     dev = pkgs.sipi.override {
       cmakeBuildType = "Debug";
       enableCoverage = true;
       enableTests = true;
     };

     release = (pkgs.sipi.override {
       cmakeBuildType = "Release";
       enableTests = false;
     }).overrideAttrs { dontStrip = true; };

     # default already has separateDebugInfo = true (RelWithDebInfo).
    # Access debug symbols via: nix build .#default.debug
  };
   ```

4. **Eliminate devShell duplication** — use `inputsFrom`:
   ```nix
   devShells.clang = pkgs.mkShell.override {
     stdenv = pkgs.llvmPackages_19.libcxxStdenv;
   } {
     name = "sipi";
     hardeningDisable = [ "all" ];
     inputsFrom = [ pkgs.sipi ];  # inherits all build deps
     packages = with pkgs; [
       # Dev-only tools not needed for the build itself
       gcovr lcov llvmPackages_19.llvm
       rustc cargo hurl
       nginx graphicsmagick apacheHttpd imagemagick
       libxml2 libxslt
     ];
   };
   ```

5. **Preserve existing shell variants** (fuzz, gcc) with same `inputsFrom`
   pattern

**Acceptance criteria:**
- `nix develop` drops into a shell where `just nix-build && just nix-test` works
- `nix build .#dev` produces a coverage-instrumented binary
- Adding a new build dependency to `package.nix` automatically appears in all
  dev shells (no manual sync)
- `overlays.default` is exported and can be consumed by external flakes

---

### Section 3: Docker Image via Nix `dockerTools`

**Files:** `flake.nix`, new `docker.nix` (optional, for readability)

**Design decision:** Keep the existing `Dockerfile` as the production Docker
build path for now. Add a Nix-based Docker image as an **alternative** that
can be used for local dev and eventually replace the Dockerfile.

**Rationale:** The existing Dockerfile is well-tested, produces known-good
images, and is tightly integrated with Docker Hub publishing. Replacing it
immediately would be high-risk. Instead, we add a Nix Docker output that
can be validated in parallel.

**Changes:**

1. **Add `dockerTools.buildLayeredImage` output:**
   ```nix
   packages.docker = pkgs.dockerTools.buildLayeredImage {
     name = "daschswiss/sipi";
     tag = self.rev or "dev";
     maxLayers = 125;
     contents = with pkgs; [
       sipi
       cacert              # SSL certificates
       dockerTools.fakeNss # /etc/passwd for getpwuid
       bashInteractive     # debugging (optional, remove for minimal)
       coreutils           # debugging (optional)
       ffmpeg              # runtime dep for video transcoding
       curl                # health check
     ];
     config = {
       Cmd = [ "${pkgs.sipi}/bin/sipi" "--config=/sipi/config/sipi.config.lua" ];
       ExposedPorts = { "1024/tcp" = {}; };
       WorkingDir = "/sipi";
       Env = [
         "SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt"
         "LC_ALL=en_US.UTF-8"
         "LANG=en_US.UTF-8"
       ];
     };
     fakeRootCommands = ''
       mkdir -p ./sipi/images/knora
       mkdir -p ./sipi/cache
       mkdir -p ./sipi/config
       mkdir -p ./sipi/scripts
       mkdir -p ./sipi/server
       # Same files as Dockerfile final stage (lines 122-127)
       cp ${pkgs.sipi}/share/sipi/config/sipi.config.lua  ./sipi/config/
       cp ${pkgs.sipi}/share/sipi/config/sipi.init.lua    ./sipi/config/
       cp ${pkgs.sipi}/share/sipi/server/test.html         ./sipi/server/
       cp ${pkgs.sipi}/share/sipi/scripts/test_functions.lua ./sipi/scripts/
       cp ${pkgs.sipi}/share/sipi/scripts/send_response.lua  ./sipi/scripts/
     '';
   };
   ```

2. **Add streaming variant for CI:**
   ```nix
   packages.docker-stream = pkgs.dockerTools.streamLayeredImage {
     # Same config as above but streams to stdout
   };
   ```

3. **Add justfile recipe:**
   ```just
   # Build Docker image via Nix
   nix-docker-build:
       $(nix build .#docker-stream --print-out-paths) | docker load
   ```

4. **Update `package.nix` install phase** to copy the exact runtime files into
   `$out/share/sipi/` (matching Dockerfile lines 122-127: `sipi.config.lua`,
   `sipi.init.lua`, `test.html`, `test_functions.lua`, `send_response.lua`)

**Acceptance criteria:**
- `nix build .#docker` produces a loadable Docker image
- Image passes existing smoke tests (`just test-smoke-ci`)
- Image size is comparable to or smaller than the current Dockerfile-based image

---

### Section 4: Static Binary Builds via Nix

**Files:** `flake.nix`, `package.nix`, potentially `static.nix`

#### 4.1 Zig-in-Nix Static Builds

**Design decision:** For Phase 1, continue using the Zig toolchain for static
musl builds but orchestrate them through Nix. This preserves the proven Zig
cross-compilation while gaining Nix caching.

**Approach A — Zig-in-Nix (recommended for Phase 1):**

The Zig toolchain is already integrated via `cmake/zig-toolchain.cmake`. We
create a Nix derivation that:
1. Uses Nix-provided Zig binary
2. Invokes the same CMake + Zig toolchain configuration
3. Targets musl for static linking
4. Benefits from Nix store caching

```nix
# Parameterized static build — called for both amd64 and arm64.
# Uses stdenv (not stdenvNoCC) because make, binutils, and pkg-config
# hooks are needed even though Zig provides the C/C++ compiler.
mkStaticBuild = { arch, zigTarget }: pkgs.stdenv.mkDerivation {
  pname = "sipi-static-${arch}";
  version = version;
  src = filteredSrc;

  nativeBuildInputs = with pkgs; [
    cmake zig autoconf automake libtool
    unzip file xxd pkgconf
  ];

  configurePhase = ''
    cmake -S . -B build \
      -G "Unix Makefiles" \
      -DCMAKE_TOOLCHAIN_FILE=cmake/zig-toolchain.cmake \
      -DZIG_TARGET=${zigTarget} \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
      -DEXT_PROVIDED_VERSION=${version}
  '';

  buildPhase = ''
    cmake --build build --parallel $NIX_BUILD_CORES
  '';

  installPhase = ''
    mkdir -p $out/bin
    cp build/sipi $out/bin/

    # Install exactly the runtime files shipped in the Docker image
    mkdir -p $out/share/sipi/config $out/share/sipi/scripts $out/share/sipi/server
    cp config/sipi.config.lua      $out/share/sipi/config/
    cp config/sipi.init.lua        $out/share/sipi/config/
    cp server/test.html            $out/share/sipi/server/
    cp scripts/test_functions.lua  $out/share/sipi/scripts/
    cp scripts/send_response.lua   $out/share/sipi/scripts/
  '';

  # Use manual objcopy for debug symbol separation (not separateDebugInfo,
  # which conflicts with manual extraction and can break on static binaries).
  outputs = [ "out" "debug" ];
  postFixup = ''
    mkdir -p $debug/lib/debug
    objcopy --only-keep-debug $out/bin/sipi $debug/lib/debug/sipi.debug
    strip $out/bin/sipi
    objcopy --add-gnu-debuglink=$debug/lib/debug/sipi.debug $out/bin/sipi
  '';
};

# Both architectures use the same derivation, built on native runners
# (amd64 on ubuntu-24.04, arm64 on ubuntu-24.04-arm — no cross-compilation).
packages.static-amd64 = mkStaticBuild { arch = "amd64"; zigTarget = "x86_64-linux-musl"; };
packages.static-arm64 = mkStaticBuild { arch = "arm64"; zigTarget = "aarch64-linux-musl"; };
```

**Approach B — pkgsStatic (future Phase 2):**

Once dependencies are migrated from `ext/` to nixpkgs, we can use:
```nix
packages.static-amd64 = pkgs.pkgsStatic.callPackage ./package.nix {
  enableTests = false;
};
```
This would eliminate Zig entirely for Linux static builds.

**Release packaging (both architectures):**

The tarball must contain exactly the same Sipi-specific files as the Docker
image's final stage (Dockerfile lines 122-127). This is the contract between
the Sipi release and downstream consumers like dsp-api.

**Tarball layout** (mirrors `/sipi/` in the Docker image):
```
sipi-vX.Y.Z-linux-amd64/
  sipi                          # static binary (stripped, debuglink attached)
  config/sipi.config.lua        # default server config
  config/sipi.init.lua          # Lua initialization script
  server/test.html              # test page
  scripts/test_functions.lua    # Lua test functions
  scripts/send_response.lua     # Lua response helper
```

Add Nix derivations that create the release tarballs for both amd64 and arm64.
The derivation is parameterized by architecture:

```nix
mkReleaseArchive = arch: let
  static = self.packages.${system}."static-${arch}";
in pkgs.runCommand "sipi-release-${arch}" { inherit static; } ''
  mkdir -p $out
  VERSION="${version}"
  DIR="sipi-$VERSION-linux-${arch}"
  mkdir -p work/$DIR/config work/$DIR/scripts work/$DIR/server

  # Binary (stripped, with debuglink)
  cp $static/bin/sipi work/$DIR/sipi

  # Exactly the files from the Docker image final stage:
  cp $static/share/sipi/config/sipi.config.lua  work/$DIR/config/
  cp $static/share/sipi/config/sipi.init.lua     work/$DIR/config/
  cp $static/share/sipi/server/test.html         work/$DIR/server/
  cp $static/share/sipi/scripts/test_functions.lua work/$DIR/scripts/
  cp $static/share/sipi/scripts/send_response.lua  work/$DIR/scripts/

  # Tarball + checksum
  tar czf $out/$DIR.tar.gz -C work $DIR
  sha256sum $out/$DIR.tar.gz > $out/$DIR.tar.gz.sha256

  # Debug symbols (separate file for Sentry + GitHub Release)
  cp ${static.debug}/lib/debug/sipi.debug $out/sipi-linux-${arch}.debug
'';

# In packages:
packages.release-archive-amd64 = mkReleaseArchive "amd64";
packages.release-archive-arm64 = mkReleaseArchive "arm64";
```

**GitHub Release artifacts (per release):**
```
sipi-vX.Y.Z-linux-amd64.tar.gz
sipi-vX.Y.Z-linux-amd64.tar.gz.sha256
sipi-vX.Y.Z-linux-arm64.tar.gz
sipi-vX.Y.Z-linux-arm64.tar.gz.sha256
sipi-linux-amd64.debug
sipi-linux-arm64.debug
```

**Note:** System-level runtime dependencies (ca-certificates, ffmpeg, tzdata,
locales, pid1) are NOT in the tarball — they are the responsibility of whoever
builds the final Docker image from it. The tarball is purely the Sipi
application layer.

#### 4.2 dsp-api Consumption Strategy

**Strategic goal:** dsp-api builds its own `knora-sipi` Docker image from the
static tarball published to GitHub Releases, instead of depending on the
`daschswiss/sipi` Docker image on Docker Hub.

**Current coupling (to be removed):**
- `project/Dependencies.scala` line 15: `val sipiImage = "daschswiss/sipi:v4.0.1"`
- `build.sbt`: knora-sipi Docker module uses `FROM daschswiss/sipi:v4.0.1`
- Integration tests pull `daschswiss/knora-sipi` container
- Ingest service runs `docker run` against `daschswiss/knora-sipi`

**Target architecture:**
```
Sipi repo                          dsp-api repo
─────────                          ─────────────
nix build .#static-{amd64,arm64}   Downloads tarball from GitHub Release
        │                                    │
        ▼                                    ▼
GitHub Release                     Dockerfile.knora-sipi
  sipi-vX.Y.Z-linux-amd64.tar.gz    FROM scratch (or minimal base)
  sipi-vX.Y.Z-linux-arm64.tar.gz    COPY sipi /sipi/sipi
  sipi-linux-amd64.debug            COPY config, scripts, server
  sipi-linux-arm64.debug            # No apt-get, no compile — just a binary
```

**Benefits:**
- **No Docker Hub dependency** — dsp-api CI doesn't need to pull from Docker
  Hub; it downloads a GitHub Release artifact (same org, authenticated)
- **Faster knora-sipi builds** — no multi-stage compile; just COPY a static
  binary into a minimal image. Build goes from ~20 min to ~10 sec
- **Version pinning via tarball URL** — `Dependencies.scala` references a
  GitHub Release URL + SHA-256 instead of a Docker tag. Immutable by design
- **Simpler testing** — integration tests can either use the Docker image
  built from tarball, or run the static binary directly (no Docker needed)
- **Static binary = no runtime deps** — the tarball contains a fully
  self-contained musl binary. The Docker image needs only the binary, config
  files, CA certs, and ffmpeg (if video transcoding is needed)

**dsp-api Dockerfile.knora-sipi (sketch):**
```dockerfile
FROM ubuntu:24.04
# Only runtime deps that can't be statically linked
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates ffmpeg tzdata && rm -rf /var/lib/apt/lists/*
WORKDIR /sipi
COPY sipi /sipi/sipi
COPY config/ /sipi/config/
COPY scripts/ /sipi/scripts/
COPY server/ /sipi/server/
# knora-sipi specific config and scripts overlaid here
COPY knora-config/ /sipi/config/
COPY knora-scripts/ /sipi/scripts/
HEALTHCHECK --interval=30s --timeout=5s CMD curl -sf http://localhost:1024/health || exit 1
ENTRYPOINT ["/sipi/sipi"]
CMD ["--config=/sipi/config/sipi.docker-config.lua"]
```

**Migration path (dsp-api side, out of scope for this plan but documented):**
1. Sipi starts publishing amd64 + arm64 tarballs to GitHub Releases (this plan)
2. dsp-api adds a build step that downloads the tarball for the target arch
3. dsp-api replaces `FROM daschswiss/sipi:vX.Y.Z` with `FROM ubuntu:24.04` +
   COPY from tarball
4. `Dependencies.scala` changes from Docker image reference to GitHub Release
   URL + SHA-256
5. Integration tests updated to build from tarball or use direct binary
6. Once validated, remove the `daschswiss/sipi` Docker image from the publish
   pipeline (or keep it for standalone use)

**Acceptance criteria:**
- `nix build .#static-amd64` produces a statically linked binary
- `nix build .#static-arm64` produces a statically linked binary
- `ldd` / `readelf` confirms no dynamic dependencies on both architectures
- Unit tests pass against static binaries (both arches)
- Debug symbols are in a separate output (both arches)
- `nix build .#release-archive-amd64` produces `.tar.gz`, `.sha256`, `.debug`
- `nix build .#release-archive-arm64` produces `.tar.gz`, `.sha256`, `.debug`
- Tarball contents are self-contained (binary + config + scripts + server)
- Static binary unit tests run via `doCheck = true` inside the Nix derivation
  (ctest executes as part of the build, same as the current Zig CI flow)

---

### Section 5: CI Workflow Migration

**Files:** `.github/workflows/test.yml`, `.github/workflows/publish.yml`

#### 5.1 Add Cachix to All Nix CI Jobs

Replace the current Nix CI pattern:
```yaml
# Before (no caching, make)
- uses: cachix/install-nix-action@v31
- run: nix develop --command bash -c "make nix-build && make nix-test"
```

With:
```yaml
# After (Cachix binary cache, just)
- uses: DeterminateSystems/determinate-nix-action@v3
- uses: cachix/cachix-action@v15
  with:
    name: dasch-swiss
    authToken: '${{ secrets.CACHIX_AUTH_TOKEN }}'  # already in repo since 2024-04-30
- run: nix develop --command just nix-build nix-test
```

**Secret required:** `CACHIX_AUTH_TOKEN` — already exists in repo (added 2024-04-30, currently unused). Cachix cache name: `dasch-swiss`.

#### 5.2 Migrate test.yml Nix Jobs

**Current:** `nix develop --command bash -c "make nix-build && make nix-test"`
**Target:** `nix develop --command just nix-build nix-test` with Cachix warm
cache. Add Cachix action steps and switch to `just`.

#### 5.3 Add Nix-Based Static Build Job (Parallel to Zig)

Add a new CI job that validates the Nix static build path:
```yaml
nix-static:
  runs-on: ${{ matrix.runner }}
  name: nix-static / ${{ matrix.arch }}
  strategy:
    matrix:
      include:
        - arch: amd64
          runner: ubuntu-24.04
        - arch: arm64
          runner: ubuntu-24.04-arm
  steps:
    - uses: actions/checkout@v6
      with: { lfs: true }
    - uses: DeterminateSystems/determinate-nix-action@v3
    - uses: cachix/cachix-action@v15
      with:
        name: dasch-swiss
        authToken: '${{ secrets.CACHIX_AUTH_TOKEN }}'  # already in repo since 2024-04-30
    - run: nix build .#static-${{ matrix.arch }}
    - name: Verify static linkage
      run: |
        file result/bin/sipi
        ! readelf -d result/bin/sipi | grep -q '(NEEDED)'
    - name: Run unit tests
      run: |
        # Tests from the build derivation, or run against built binary
        cd result/bin && ./sipi --version
```

#### 5.4 Migrate publish.yml to Nix

**Phase 1 (this plan):** Add Nix static build as validation alongside existing
Zig build. Both must pass the release gate.

**Phase 2 (future):** Replace Zig static builds with Nix static builds once
validated over several releases.

**Updated publish.yml structure:**
```
validate-docker (existing) ─┐
validate-static-zig (existing) ─┤
validate-static-nix (new) ──────┤
                                 └─ release-gate ─┬─ publish-docker
                                                   ├─ publish-static-release
                                                   ├─ manifest
                                                   └─ sentry
```

#### 5.5 Debug Symbols in GitHub Release

The current workflow already attaches debug symbols to GitHub Releases (via
`publish-static-release` job). The Nix build produces them via
`separateDebugInfo` or manual `objcopy`. No change needed to the release
attachment logic — just ensure the Nix build outputs debug files in the
expected locations.

**Acceptance criteria:**
- Nix CI jobs have Cachix caching enabled
- Second CI run (cache warm) completes in < 5 min
- Nix static build job passes alongside existing Zig job
- GitHub Release contains: `.tar.gz`, `.sha256`, `.debug` for each arch

---

### Section 6: Local Developer Experience — Migrate Makefile to justfile

**Files:** `Makefile` (delete), `vars.mk` (delete), new `justfile`,
`flake.nix`, `docs/src/development/building.md`, `CLAUDE.md`

#### 6.1 Why justfile

The current `Makefile` (354 lines, 50+ targets) is used purely as a command
runner — no targets use Make's dependency graph or incremental rebuild
features. `just` is a better fit:

- **Simpler syntax** — no `.PHONY`, no `$$` escaping, no tab-vs-space traps
- **Built-in `--list`** — replaces the hand-rolled `help` target
- **Consistent with dsp-api** — which already uses `just` as its command runner
- **Recipe parameters** — `just zig-build-static x86_64-linux-musl` instead
  of `make zig-build-static ZIG_TARGET=x86_64-linux-musl`
- **Available in Nix** — `pkgs.just` can be added to the dev shell

#### 6.2 Cachix for Local Dev

Developers add the Cachix cache as a substituter:
```bash
# One-time setup
cachix use dasch-swiss
```

This adds `dasch-swiss.cachix.org` to `~/.config/nix/nix.conf`. Subsequent
`nix develop` commands pull pre-built `ext/` dependencies from the cache
instead of building them locally (~15 min saved on first setup).

#### 6.3 justfile Structure

Translate all existing Makefile targets 1:1 into `justfile` recipes, then add
the new Nix recipes. The justfile is grouped by the same sections as the
current Makefile.

```just
# Auto-detect CPU cores
nproc := `nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4`

# Docker repo/tag (from vars.mk)
docker_repo := "daschswiss/sipi"
build_tag := `git describe --tag --dirty --abbrev=7 2>/dev/null || cat version.txt`
docker_image := docker_repo + ":" + build_tag

# List all recipes
default:
    @just --list

#####################################
# Docker
#####################################

# Build and load Sipi Docker image locally
docker-build:
    docker buildx build \
        --progress auto \
        --build-arg VERSION={{build_tag}} \
        -t {{docker_image}} -t {{docker_repo}}:latest \
        --load .

# Build + test amd64 Docker image, extract debug symbols
docker-test-build-amd64:
    ...  # (translated from Makefile, same logic)

#####################################
# Nix (run inside `nix develop`)
#####################################

# Build SIPI (debug + coverage, inside Nix shell)
nix-build:
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCODE_COVERAGE=ON --log-context
    cmake --build ./build --parallel {{nproc}}

# Run unit tests (inside Nix shell)
nix-test:
    cd build && ctest --output-on-failure

# Run Rust e2e tests (requires built sipi in build/)
rust-test-e2e:
    cd test/e2e-rust && SIPI_BIN={{justfile_directory()}}/build/sipi cargo test -- --test-threads=1

# Run Hurl HTTP contract tests (requires built sipi in build/)
hurl-test:
    ...  # (translated from Makefile)

#####################################
# Nix full builds (no shell required)
#####################################

# Build release binary via nix build
nix-build-release:
    nix build .#default
    @echo "Binary at: $(readlink result)/bin/sipi"

# Build static amd64 binary via Nix
nix-build-static-amd64:
    nix build .#static-amd64

# Build static arm64 binary via Nix
nix-build-static-arm64:
    nix build .#static-arm64

# Build Docker image via Nix
nix-docker-build:
    $(nix build .#docker-stream --print-out-paths) | docker load

#####################################
# Zig toolchain
#####################################

# Build SIPI for local dev using Zig (no Nix required)
zig-build-local:
    cmake -B build -S . \
        -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE=cmake/zig-toolchain.cmake \
        -DCMAKE_BUILD_TYPE=Debug
    cmake --build build --parallel {{nproc}}

# Build fully static Linux binary for a given target
zig-build-static target:
    cmake -B build-static -S . \
        -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE=cmake/zig-toolchain.cmake \
        -DZIG_TARGET={{target}} \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
    cmake --build build-static --parallel {{nproc}}

# Build static SIPI binary for Linux amd64
zig-build-amd64: (zig-build-static "x86_64-linux-musl")

# Build static SIPI binary for Linux arm64
zig-build-arm64: (zig-build-static "aarch64-linux-musl")

#####################################
# Vendor dependencies
#####################################

# Download all dependency archives to vendor/
vendor-download:
    @scripts/vendor.sh download

# Verify SHA-256 checksums of vendored archives
vendor-verify:
    @scripts/vendor.sh verify
```

**Key improvements over Makefile:**
- `zig-build-static` takes `target` as a recipe parameter instead of a Make
  variable (`ZIG_TARGET`), so `just zig-build-static aarch64-linux-musl` works
- `nproc` and `build_tag` use backtick evaluation (computed once at start)
- `justfile_directory()` replaces `$(CURDIR)` / `$(PWD)` for reliable paths
- No `.PHONY` boilerplate, no `$$` escaping for shell variables
- `just --list` auto-generates help from recipe comments

#### 6.4 CI Workflow Updates

CI workflows currently call `make <target>`. Update all workflow files to call
`just <target>` instead. Since `just` is a single static binary, add an
install step or use the Nix dev shell:

```yaml
# Option A: Install just via GitHub Action
- uses: extractions/setup-just@v2

# Option B: Already available in nix develop shell
- run: nix develop --command just nix-build
```

For Docker-focused CI jobs that don't use Nix, use `setup-just`. For
Nix-based jobs, add `just` to the dev shell packages in `flake.nix`.

#### 6.5 Add `just` to Nix Dev Shell

In `flake.nix`, add `just` to the dev shell packages:
```nix
packages = with pkgs; [
  just  # command runner (replaces Makefile)
  # ... existing packages ...
];
```

#### 6.6 Documentation Update

Update `docs/src/development/building.md` to document:
- `just` as the command runner (replacing `make`)
- Cachix setup for fast first builds
- `nix build` targets and what they produce
- How `nix develop` + `just` targets differ from `nix build` targets
- Debugging with separate debug symbols

Update `CLAUDE.md` quick reference to use `just` instead of `make`.

**Acceptance criteria:**
- `Makefile` and `vars.mk` are deleted
- `justfile` contains all recipes from the old Makefile
- `just` (with no args) lists all available recipes with descriptions
- `just nix-build && just nix-test` works inside `nix develop`
- All CI workflows updated from `make` to `just`
- Fresh clone + `cachix use dasch-swiss` + `nix develop` completes in < 2 min
- `just` is available in the Nix dev shell

---

### Section 7: Kakadu Publishing via GitHub Releases (dsp-ci-assets)

**Repositories:** dsp-ci-assets
**Files:** new `scripts/publish-kakadu.sh`, new `justfile` (replaces the
root `Makefile`), `README.md`, new `kakadu/README.md`, `.gitignore`.
Also drop the obsolete `netdata/` and `graphdb/` directories (no longer
used). After this work, `dsp-ci-assets` hosts only the Kakadu archives.

#### 7.1 Problem

`dsp-ci-assets` commits 10 Kakadu archives directly (≈128 MB working tree,
≈99 MB `.git`). Cloning is slow, the repo has no versioned release semantics
for a proprietary dependency, and — most importantly — Sipi's Nix build
cannot consume these archives purely: they live in a separate git repo and
embedding them via a flake input would drag in the entire ≈220 MB closure.

#### 7.2 Target Architecture

Each Kakadu archive is published as a GitHub Release asset on the private
`dasch-swiss/dsp-ci-assets` repo:

- **Release tag:** `kakadu-v8.5` — human-readable, matches upstream version
- **Title:** `Kakadu v8.5 (01382N build)`
- **Asset:** the `.zip` file, unmodified (e.g. `v8_5-01382N.zip`)
- **Notes:** SHA-256, upstream version, license reminder
- **Visibility:** inherited from the private repo — only org members can
  download; the default `GITHUB_TOKEN` of a different repo's workflow does
  **not** work cross-repo (see 7.6)

GitHub's `gh` CLI and REST API handle private release-asset auth with a
user token or a fine-grained PAT. No server-side infra to run.

#### 7.3 Publishing Script

Add `scripts/publish-kakadu.sh` (executable, `set -euo pipefail`):

```bash
#!/usr/bin/env bash
set -euo pipefail

# Usage: scripts/publish-kakadu.sh <archive-path>
# Example: scripts/publish-kakadu.sh kakadu/v8_5-01382N.zip

ARCHIVE="${1:?usage: $0 <archive-path>}"
[[ -f "$ARCHIVE" ]] || { echo "File not found: $ARCHIVE" >&2; exit 1; }

BASE="$(basename "$ARCHIVE" .zip)"          # v8_5-01382N
VERSION_SLUG="${BASE%-*}"                   # v8_5
VERSION_DOT="${VERSION_SLUG//_/.}"          # v8.5
BUILD_SUFFIX="${BASE##*-}"                  # 01382N
TAG="kakadu-${VERSION_DOT}"
TITLE="Kakadu ${VERSION_DOT} (${BUILD_SUFFIX} build)"

SHA256="$(shasum -a 256 "$ARCHIVE" | awk '{print $1}')"
SIZE="$(wc -c <"$ARCHIVE" | tr -d ' ')"

NOTES="$(mktemp)"
trap 'rm -f "$NOTES"' EXIT
cat >"$NOTES" <<EOF
Kakadu SDK archive for DaSCH internal use.

- **Upstream version:** ${VERSION_DOT}
- **Build:** ${BUILD_SUFFIX}
- **SHA-256:** \`${SHA256}\`
- **Size:** ${SIZE} bytes

Licensed under DaSCH's commercial Kakadu licence. **Do not redistribute
outside the dasch-swiss GitHub organisation.**

Consumers pin this asset by hash — see \`kakadu/README.md\`.
EOF

gh release create "$TAG" "$ARCHIVE" \
    --repo dasch-swiss/dsp-ci-assets \
    --title "$TITLE" \
    --notes-file "$NOTES"

echo
echo "Published ${TAG}."
echo "SHA-256: ${SHA256}"
echo "Add this to kakadu/README.md and pin in consumers."
```

Replace the existing `Makefile` with a `justfile` (consistent with
sipi's Section 6 migration):

```just
# List all recipes
default:
    @just --list

# Publish a Kakadu archive to GitHub Releases
# Usage: just publish-kakadu kakadu/v8_5-01382N.zip
publish-kakadu archive:
    @scripts/publish-kakadu.sh "{{archive}}"
```

Delete the root `Makefile` and the obsolete `netdata/` and `graphdb/`
directories in the same change.

Publisher workflow:

1. Obtain the Kakadu `.zip` from the DaSCH licence holder
2. Place it using the exact upstream filename — format
   `vX_Y[_Z]-NNNNNC.zip`, where `NNNNN` is a 5-digit build number and
   `C` is a single-letter licence/build code (observed in this repo:
   `L` and `N`; do not normalise)
3. `just publish-kakadu kakadu/<filename>.zip`
4. Record the printed SHA-256 in `kakadu/README.md` (the version table)
5. Open a PR with the README update only — the `.zip` stays out of git

#### 7.4 Migration of Existing Archives

Republish every currently-tracked archive with a one-off loop:

```bash
for f in kakadu/*.zip; do
  just publish-kakadu "$f"
done
```

Then remove the archives from the **working tree** and ignore them going
forward. Update `.gitignore`:

```
# Kakadu archives are distributed via GitHub Releases, not committed.
kakadu/*.zip
!kakadu/README.md
```

**Rewrite history to drop the blobs** (≈99 MB → ≈1 MB):

```bash
brew install git-filter-repo            # once
git filter-repo --path-glob 'kakadu/*.zip' --invert-paths
git push --force origin main
```

Re-clone any local worktree after the push.

#### 7.5 Documentation

Replace the current `README.md` (3 lines) with:

```markdown
# dsp-ci-assets

Non-public assets for DaSCH CI. Access is gated by membership in the
`dasch-swiss` GitHub organisation.

The only asset hosted here is the proprietary **Kakadu SDK**, published
as GitHub Releases (not committed). See
[`kakadu/README.md`](kakadu/README.md) for publishing and consumption.

Publish a new archive with `just publish-kakadu kakadu/...`.
```

Add a new `kakadu/README.md`:

```markdown
# Kakadu SDK

Proprietary JPEG2000 SDK used by Sipi. Licensed to DaSCH under commercial
terms — **do not redistribute outside the `dasch-swiss` GitHub org.**

## Published versions

| Version | Release tag    | Asset filename          | SHA-256 |
|---------|----------------|-------------------------|---------|
| v8.5    | `kakadu-v8.5`  | `v8_5-01382N.zip`       | `<fill in from release notes>` |
| v8.4.1  | `kakadu-v8.4.1`| `v8_4_1-01382N.zip`     | `...` |
| v8.3    | `kakadu-v8.3`  | `v8_3-01727L.zip`       | `...` |
| ...     |                |                         |       |

Update this table every time a new version is published.

## Publishing

1. Obtain the archive from the DaSCH licence holder (internal Notion page:
   "Kakadu licence")
2. Place it in this directory using the exact upstream filename —
   format `vX_Y[_Z]-NNNNNC.zip`, where `NNNNN` is a 5-digit build number
   and `C` is the licence/build code letter (`L`, `N`, …). Don't
   normalise it — consumers pin the exact filename
3. From the repo root: `just publish-kakadu kakadu/<filename>.zip`
4. Copy the printed SHA-256 into the table above
5. Commit the README change and open a PR — do **not** commit the `.zip`

## Consuming

Authenticated URL pattern:

`https://github.com/dasch-swiss/dsp-ci-assets/releases/download/kakadu-vX.Y/<filename>.zip`

(e.g. `kakadu-v8.5/v8_5-01382N.zip` or `kakadu-v8.3/v8_3-01727L.zip`)

Authentication is a GitHub token with read access to this repo:

- **Local:** `gh auth token` (the `gh` CLI's stored user token)
- **CI:** a fine-grained PAT with `Contents: Read` on `dsp-ci-assets`,
  stored as a secret in the consumer repo (see 7.6)

Consumers MUST pin by SHA-256 — releases are immutable by convention, and
pinning surfaces tampering as a hash mismatch.

**Primary consumer:** Sipi. Integration details in
`sipi/docs/src/development/kakadu.md`.

## Licensing

Kakadu is not open source. Every user of these archives (developer or CI
job) is bound by DaSCH's commercial Kakadu licence. Adding a new
collaborator means adding them to the `dasch-swiss` GitHub org — the repo's
visibility is the licence-compliance boundary.
```

#### 7.6 Access Control

Release visibility inherits from the private repo. CI uses the existing
org-level `DASCHBOT_PAT` secret, already referenced by every sipi
workflow. No new secret.

#### 7.7 Acceptance Criteria

- `scripts/publish-kakadu.sh` is executable and passes `shellcheck`
- `just publish-kakadu <path>` publishes a release with correct tag,
  title, notes, and asset
- Every currently-tracked Kakadu archive has a corresponding release
- `kakadu/README.md` lists every published version with SHA-256
- A developer with org access can run
  `gh release download kakadu-v8.5 -R dasch-swiss/dsp-ci-assets -p 'v8_5*.zip'`
  and get the archive
- `.gitignore` ignores `kakadu/*.zip`; `git status` on a clean checkout
  shows no zips as either tracked or untracked
- `README.md` points consumers at `kakadu/README.md`

---

### Section 8: Pure Nix Builds via Kakadu Release Fetch (sipi)

**Repositories:** sipi
**Files:** `flake.nix`, `package.nix`, `justfile`, `.github/workflows/test.yml`,
`.github/workflows/publish.yml`, new `docs/src/development/kakadu.md`,
`docs/src/development/building.md`, `CLAUDE.md`

**Supersedes** the `KAKADU_ARCHIVE` + `--impure` approach currently in
Sections 1 and 2 (see current state in `flake.nix:11-15`, `package.nix:17,99-102`,
`justfile:240-253`, `.github/workflows/test.yml:109`).

#### 8.1 Goal

A fresh clone of `sipi` — with no environment variables, no pre-populated
`vendor/`, and no `--impure` — must build everything via Nix:

```bash
nix build .#default       # works
nix build .#static-amd64  # works
nix build .#static-arm64  # works
nix build .#docker        # works
```

…provided the developer has a GitHub token configured for Nix (one-time).
This closes the "Pure vs Impure" open decision noted in Section 10.

#### 8.2 Fetch Kakadu in `flake.nix`

Replace the `builtins.getEnv "KAKADU_ARCHIVE"` block
(`flake.nix:11-15`) with a `pkgs.fetchurl` call:

```nix
let
  # Pinned Kakadu release on dsp-ci-assets.
  # Updating: bump version + hash, rerun `nix build .#default` to verify.
  kakaduVersion   = "v8.5";
  kakaduAssetName = "v8_5-01382N.zip";
  kakaduHash      = "sha256:c19c7579d1dee023316e7de090d9de3eb24764e349b4069e5af3a540fb644e75";

  kakaduArchive = pkgs.fetchurl {
    url = "https://github.com/dasch-swiss/dsp-ci-assets/releases/download/kakadu-${kakaduVersion}/${kakaduAssetName}";
    hash = kakaduHash;
    # GitHub redirects release downloads to objects.githubusercontent.com;
    # curl strips auth on redirect by default, which 401s on the private
    # repo. --location-trusted forwards the netrc credential across the
    # redirect. The fixed hash ensures we can't be tricked by a malicious
    # redirect target.
    curlOptsList = [ "--location-trusted" ];
  };
in {
  packages.default         = pkgs.sipi.override { inherit kakaduArchive; };
  packages.dev             = (pkgs.sipi.override { inherit kakaduArchive; cmakeBuildType = "Debug";   enableCoverage = true; }).overrideAttrs (_: { dontStrip = true; });
  packages.release         = (pkgs.sipi.override { inherit kakaduArchive; cmakeBuildType = "Release"; enableTests = false;  }).overrideAttrs (_: { dontStrip = true; });
  packages.static-amd64    = mkStaticBuild { arch = "amd64"; zigTarget = "x86_64-linux-musl";  inherit kakaduArchive; };
  packages.static-arm64    = mkStaticBuild { arch = "arm64"; zigTarget = "aarch64-linux-musl"; inherit kakaduArchive; };
  # ... docker, release-archive-* all thread kakaduArchive the same way
}
```

`pkgs.fetchurl` is a fixed-output derivation (FOD): network access is
permitted, but the declared `hash` pins the result. The build is pure —
purity is a property of reproducibility, not of network isolation.

#### 8.3 Require `kakaduArchive` in `package.nix`

Tighten `package.nix:17,99-102`:

```nix
{ ..., kakaduArchive }:   # no longer ? null; a required input
...
preConfigure = ''
  cp ${kakaduArchive} vendor/v8_5-01382N.zip
'';
```

Remove the `lib.optionalString (kakaduArchive != null) ...` guard. A
missing archive is now a type error at evaluation time, not a silent
skip that produces a Kakadu-less binary.

#### 8.4 Drop `--impure` Everywhere

Update `justfile:240-253`:

```just
# Build release binary via nix build
nix-build-release:
    nix build .#default
    @echo "Binary at: $(readlink result)/bin/sipi"

# Build static amd64 binary via Nix
nix-build-static-amd64:
    nix build .#static-amd64

# Build static arm64 binary via Nix
nix-build-static-arm64:
    nix build .#static-arm64

# Build Docker image via Nix
nix-docker-build:
    $(nix build .#docker-stream --print-out-paths) | docker load
```

No `KAKADU_ARCHIVE=`, no `--impure`.

Update `.github/workflows/test.yml:109`:

```yaml
- name: Build static Sipi via Nix
  run: nix build .#static-${{ matrix.arch }}
```

Remove the `KAKADU_ARCHIVE=$PWD/vendor/v8_5-01382N.zip` prefix and the
`--impure` flag.

#### 8.5 Developer Authentication

`pkgs.fetchurl` uses curl, which honours `netrc-file` from `nix.conf`.
One-time setup per developer machine:

```bash
# Write a netrc entry for github.com using gh's stored credentials
install -m 600 /dev/null ~/.netrc
cat >> ~/.netrc <<EOF
machine github.com
    login $(gh api user -q .login)
    password $(gh auth token)
EOF

# Point Nix at it (once)
mkdir -p ~/.config/nix
grep -q '^netrc-file ' ~/.config/nix/nix.conf 2>/dev/null \
  || echo "netrc-file = $HOME/.netrc" >> ~/.config/nix/nix.conf
```

Alternative (documented, not required): a `direnv` recipe that regenerates
the netrc from `gh auth token` on every `direnv reload`. This keeps the
stored token fresh as `gh auth refresh` rotates it. Add `.envrc.sample`:

```bash
# Copy to .envrc to enable: cp .envrc.sample .envrc && direnv allow
use flake

# Regenerate GitHub netrc from gh credentials on reload, so nix fetchurl
# can pull the Kakadu archive from dsp-ci-assets releases.
if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then
  install -m 600 /dev/null "$HOME/.netrc"
  {
    echo "machine github.com"
    echo "    login $(gh api user -q .login)"
    echo "    password $(gh auth token)"
  } > "$HOME/.netrc"
fi
```


#### 8.6 CI Authentication

Add a netrc-injection step to every Nix job in `.github/workflows/test.yml`
and `publish.yml`, between the Nix installer and any `nix build` / `nix
develop` step. Affected jobs:

- `nix-clang` (test.yml)
- `nix-static` (test.yml)
- `publish-static-nix` (publish.yml)
- any future Nix-based job

```yaml
- uses: DeterminateSystems/determinate-nix-action@v3

- name: Configure Nix netrc for dsp-ci-assets releases
  env:
    DASCHBOT_PAT: ${{ secrets.DASCHBOT_PAT }}
  run: |
    install -m 600 /dev/null "$HOME/.netrc"
    {
      echo "machine github.com"
      echo "    password $DASCHBOT_PAT"
    } > "$HOME/.netrc"
    sudo mkdir -p /etc/nix
    echo "netrc-file = $HOME/.netrc" | sudo tee -a /etc/nix/nix.conf

- uses: cachix/cachix-action@v15
  with:
    name: dasch-swiss
    authToken: '${{ secrets.CACHIX_AUTH_TOKEN }}'
```

**Secret:** reuse `DASCHBOT_PAT` (already referenced in every sipi
workflow).

The Dockerfile-based build path (`docker-build.yml`, `publish.yml` Docker
stages) is unaffected — it still bakes Kakadu in via `vendor/` and
`buildx --secret`. Unifying that path is Phase 2.

#### 8.7 Cachix Amplifies the Win

Once one CI run has built Kakadu (and every derivation transitively
depending on it), the resulting store paths land in Cachix. Every
subsequent run and every developer who `cachix use dasch-swiss`-enabled
their machine gets the already-built paths — the authenticated fetch only
happens on cache miss (Kakadu version bump, flake integration change).
The 401-on-network-blip failure mode is therefore rare in practice.

#### 8.8 New Documentation: `docs/src/development/kakadu.md`

````markdown
# Kakadu SDK and Sipi Builds

Sipi's JPEG2000 support uses the proprietary **Kakadu SDK**. The SDK is
not redistributable, so it lives in the private
[`dasch-swiss/dsp-ci-assets`](https://github.com/dasch-swiss/dsp-ci-assets)
repo and is fetched by Nix at build time — pinned by SHA-256, so builds
are reproducible.

## One-time setup (local development)

Before `nix build` / `nix develop` can succeed, tell Nix how to
authenticate to `github.com`:

```bash
install -m 600 /dev/null ~/.netrc
cat >> ~/.netrc <<EOF
machine github.com
    login $(gh api user -q .login)
    password $(gh auth token)
EOF

mkdir -p ~/.config/nix
grep -q '^netrc-file ' ~/.config/nix/nix.conf 2>/dev/null \
  || echo "netrc-file = $HOME/.netrc" >> ~/.config/nix/nix.conf
```

Requirements:
- Membership in the `dasch-swiss` GitHub organisation
- `gh auth login` completed

Verify:
```bash
nix build .#default           # should succeed without --impure
```

Optional: use the `.envrc.sample` in the repo root with `direnv` to
regenerate the netrc on every shell entry.

## Updating the Kakadu version

1. Publish a new archive on `dsp-ci-assets` (see
   [its `kakadu/README.md`](https://github.com/dasch-swiss/dsp-ci-assets/blob/main/kakadu/README.md))
2. In `flake.nix`, update `kakaduVersion`, `kakaduAssetName`, and
   `kakaduHash`
3. Run `nix build .#default` — a hash mismatch means step 2 is wrong
4. Commit and open a PR

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `unable to download … 404` | tag or asset filename wrong in `flake.nix` | Check the release on dsp-ci-assets |
| `unable to download … 401` | netrc missing or token expired | Regenerate netrc (setup section) |
| `unable to download … 401` **after redirect** | `--location-trusted` dropped from `curlOptsList` | Restore the flag |
| `hash mismatch` | release asset replaced (shouldn't happen) or `kakaduHash` wrong | Verify against `dsp-ci-assets/kakadu/README.md` |

## CI

The org-level `DASCHBOT_PAT` secret is written to `~/.netrc` at the
start of every Nix job (see `.github/workflows/test.yml` and
`publish.yml`).

## Why not vendor it directly?

- Sipi is a public repo; Kakadu is proprietary — committing it would be a
  licence breach
- Keeping it in `dsp-ci-assets` keeps the licence-compliance boundary
  aligned with repo membership
- Fetching via Nix `fetchurl` + hash pin gives reproducible, cacheable,
  pure builds without the 12 MB re-commit churn per version bump
````

#### 8.9 Documentation Updates

`docs/src/development/building.md`:

- Add an early-section box: *"Before building with Nix for the first time,
  complete the Kakadu setup in [kakadu.md](./kakadu.md)."*
- Remove every `--impure` example and every `KAKADU_ARCHIVE=...` prefix
- Keep the non-Nix (Docker buildx, Zig direct) paths as-is — they still
  use `vendor/`

`CLAUDE.md`:

- Add under "Local dev quickstart": *"First-time Nix setup: see
  `docs/src/development/kakadu.md` (one-time netrc config)."*
- Remove any `KAKADU_ARCHIVE=` references

#### 8.10 Acceptance Criteria

- A fresh clone on a dev machine with netrc configured runs
  `nix build .#default` to success — no env vars, no flags, no
  pre-populated `vendor/`
- `nix build .#static-amd64`, `.#static-arm64`, and `.#docker` also succeed
  from a fresh clone
- `nix flake show` lists the same derivations as before
- CI's `nix-clang`, `nix-static`, and publish jobs authenticate via
  `DASCHBOT_PAT`, fetch the Kakadu archive, and produce identical binaries
  to the impure baseline (verified by diffing
  `strings result/bin/sipi | grep -i kakadu`)
- `grep -rn 'KAKADU_ARCHIVE\|--impure' sipi/` returns **zero** hits in
  `flake.nix`, `package.nix`, `justfile`, `.github/workflows/`, and all
  docs (hits in historical changelogs or learnings are fine)
- `docs/src/development/kakadu.md` exists and is linked from
  `building.md` and `CLAUDE.md`
- Temporarily removing `~/.netrc` or pointing `netrc-file` at an empty
  file produces a clear 401 error from `nix build`, not a silent success
  or an obscure cache miss
- `kakaduHash` in `flake.nix` matches the SHA-256 published in
  `dsp-ci-assets/kakadu/README.md`

---

## 4. Migration Strategy

### Phase 0: Prerequisite Fix
0. [x] Add `-fno-omit-frame-pointer` to RelWithDebInfo flags (standalone PR)

### Phase 1a: Foundation (This Plan)
1. [x] Fix `package.nix` (source filtering, version, deps, parallelism)
2. [x] Restructure `flake.nix` (overlay, inputsFrom, package variants)
3. [x] Migrate `Makefile` to `justfile` (consistent with dsp-api)
4. [x] Add Cachix to CI (immediate cache benefit)
5. [x] Update CI workflows from `make` to `just`
6. [ ] Validate that existing recipes still work

### Phase 1b: Static + Docker
7. [x] Add Nix static build derivations (Zig-in-Nix) — in flake.nix, untested on Linux
8. [x] Add Nix Docker image output — in flake.nix, untested
9. [x] Add validation jobs to CI (parallel to existing) — nix-static job added to test.yml
10. [x] Add debug symbol separation to Nix builds — separateDebugInfo + manual objcopy for static

### Phase 1c: Kakadu Release Migration (Unblocks Pure Builds)
*Cross-repo work across `dsp-ci-assets` and `sipi`. Must complete before
merging Phase 1a/1b — `--impure` builds are a blocker for CI reliability
and developer onboarding.*

**In `dsp-ci-assets` (Section 7):**
11a. [x] Add `scripts/publish-kakadu.sh`; replace root `Makefile` with `justfile` (`publish-kakadu` recipe); delete obsolete `netdata/` and `graphdb/`
12a. [x] Publish every existing `kakadu/*.zip` as a GitHub Release (all 10 versions)
13a. [x] Update `.gitignore`; rewrite history with `git filter-repo` to drop all `kakadu/*.zip` blobs; push rewritten tree as new `main` branch, flip default, delete `master` (no force-push needed)
14a. [x] Rewrite `README.md`; add `kakadu/README.md` with version table and pinned SHA-256s

**In `sipi` (Section 8):**
15a. [ ] Confirm `DASCHBOT_PAT` has read access to `dsp-ci-assets` releases (smoke-test in a CI job)
16a. [x] Switch `flake.nix` to pure Kakadu fetch — `builtins.path { sha256; recursive = false; }` reading `vendor/v8_5-01382N.zip`, populated by `just kakadu-fetch`
17a. [x] Tighten `package.nix` to require `kakaduArchive`
18a. [x] Drop `--impure` and `KAKADU_ARCHIVE=` from `justfile` and workflows
19a. [x] Add `./scripts/fetch-kakadu.sh` step to every Nix CI job (replaces the briefly-tried netrc-injection approach)
20a. [x] Add `docs/src/development/kakadu.md`; update `building.md` and `CLAUDE.md`
21a. [x] Validate: `just kakadu-fetch && nix build .#default` succeeds on macOS arm64 (no env vars, no `--impure`)

### Phase 1d: Promotion
22. Validate Nix static builds over 2-3 releases
23. Replace Zig CI static builds with Nix static builds
24. Optionally replace Dockerfile with Nix Docker image
25. Update all documentation

### Phase 1e: dsp-api Tarball Consumption (Separate Plan)
26. dsp-api downloads static tarballs from Sipi GitHub Releases
27. dsp-api builds `knora-sipi` Docker image from tarball (not FROM sipi image)
28. `Dependencies.scala` references GitHub Release URL + SHA-256
29. Integration tests updated to use tarball-built image or direct binary
30. Remove `daschswiss/sipi` Docker image from publish pipeline (or keep for
    standalone users)

### Phase 2 (Future — Not This Plan)
- Migrate individual `ext/` dependencies to nixpkgs packages
- Use `pkgsStatic` instead of Zig for musl builds
- Remove Zig toolchain dependency
- Cross-compilation for macOS from Linux CI

---

## 5. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|-----------|
| Nix build produces different binary behavior | Low | High | Run full test suite (unit + e2e + hurl + smoke) against Nix-built binary |
| Cachix free tier insufficient (5GB) | Medium | Low | `ext/` closure is ~2-3GB; monitor usage. Upgrade or self-host if needed |
| Kakadu (proprietary) breaks in Nix sandbox | Medium | Medium | Add `__noChroot = true` or use `--option sandbox false` for Kakadu builds; test early |
| Nix sandbox blocks ExternalProject network access | High | High | Vendored archives in `vendor/` are included in the Nix source tree and accessible inside the sandbox. The CMake build already falls back to `vendor/` when archives are present. Verify each `ExternalProject_Add` call uses local `URL` paths — if any hardcode remote URLs without fallback, they must be patched |
| Docker image from Nix missing runtime deps | Medium | Medium | Smoke tests catch this; run existing `test-smoke-ci` against Nix Docker image |
| `separateDebugInfo` incompatible with Sentry upload | Low | Medium | Sentry CLI accepts any ELF with `.debug` extension; test upload in staging |
| Git LFS + Nix source filtering interaction | Low | Low | LFS-tracked files in `vendor/` are included in fileset; test with clean clone |
| curl strips auth on GitHub's 302 to `objects.githubusercontent.com` | Medium | High | Use `--location-trusted` in `pkgs.fetchurl` `curlOptsList`; fixed-output hash prevents redirect hijack |
| Kakadu release asset silently replaced | Low | Critical | Releases are immutable by convention; hash mismatch surfaces tampering |
| External contributor (no org membership) cannot build | Certain | Low | Documented in `kakadu.md`; non-JPEG2000 contributors can use Docker/Zig paths |

---

## 6. Success Metrics

| Metric | Current | Target |
|--------|---------|--------|
| CI time (Nix test job, cache warm) | ~20 min | < 5 min |
| CI time (Nix test job, cache cold) | ~20 min | ~20 min (same, but only on flake.lock update) |
| Local first `nix develop` (with Cachix) | ~15 min | < 2 min |
| Build reproducibility | Partial (Docker yes, Nix/Zig no guarantee) | Full (Nix hermetic) |
| Nix build purity | Impure (`--impure` + `KAKADU_ARCHIVE=`) | **Pure** (`nix build` with no flags; Kakadu fetched from GitHub Release, hash-pinned) |
| Kakadu distribution mechanism | Committed to `dsp-ci-assets` git tree (~128 MB) | GitHub Releases on `dsp-ci-assets`; `.zip` files `.gitignore`d |
| Number of build entry points | 3 (Docker, Nix shell+Make, Zig+Make) | 1 (Nix, with `just` as convenience layer) |
| Command runner consistency | Makefile (sipi) vs justfile (dsp-api) | justfile everywhere |
| Release artifacts | Docker + static amd64 .tar.gz + .debug to Sentry | Docker + static amd64+arm64 .tar.gz + .sha256 + .debug in GitHub Release |
| dsp-api Sipi dependency | Docker Hub image (`daschswiss/sipi:vX.Y.Z`) | GitHub Release tarball (future dsp-api migration) |

---

## 7. Dependencies and Prerequisites

- [x] Cachix cache `dasch-swiss` exists
- [x] `CACHIX_AUTH_TOKEN` secret exists in repo (added 2024-04-30, currently unused)
- [ ] Zig must be available as a Nix package (it is: `pkgs.zig`)
- [ ] Verify Kakadu vendor archive works inside Nix sandbox
- [ ] Verify `ext/` ExternalProject_Add calls fall back to `vendor/` inside Nix
  sandbox (no network access) — test early with `nix build .#default`
- [ ] No changes needed to `cmake/dependencies.cmake` or `ext/` build system
- [ ] Every existing Kakadu archive published as a GitHub Release on
  `dasch-swiss/dsp-ci-assets` with a stable tag (`kakadu-vX.Y`) and
  documented SHA-256 (Section 7)
- [x] `DASCHBOT_PAT` already available in sipi workflows (org-level)
- [ ] Developers have `gh auth login` completed and `dasch-swiss` org
  membership (documented in `sipi/docs/src/development/kakadu.md`)

---

## 8. Files Changed

| File | Change |
|------|--------|
| `CMakeLists.txt` | Add `-fno-omit-frame-pointer` to RelWithDebInfo flags (Section 0, standalone PR) |
| `package.nix` | Major rewrite: source filtering, parameterization, correct deps, separateDebugInfo |
| `flake.nix` | Major restructure: overlay, inputsFrom, package variants, Docker output, static outputs, add `just` to dev shell |
| `Makefile` | **Delete** — replaced by justfile |
| `vars.mk` | **Delete** — variables inlined in justfile |
| `justfile` | **New** — all recipes from Makefile + new Nix build recipes |
| `docker.nix` | **New** (optional) — Docker image derivation, split from flake.nix for readability |
| `static.nix` | **New** (optional) — static build derivation, split from flake.nix for readability |
| `.github/workflows/test.yml` | Add Cachix action, `make` -> `just`, optionally add nix-static validation job |
| `.github/workflows/publish.yml` | `make` -> `just`, add nix-static validation job (parallel to zig) |
| `.github/workflows/docker-build.yml` | `make` -> `just` |
| `docs/src/development/building.md` | Document Nix-centric workflow, Cachix setup, `just` as command runner; drop `--impure`/`KAKADU_ARCHIVE` examples; link to `kakadu.md` |
| `docs/src/development/kakadu.md` | **New** — Kakadu setup for local dev + CI (netrc config, troubleshooting, version bump flow) |
| `CLAUDE.md` | Update quick reference: `just` instead of `make`, new Nix targets, pointer to `kakadu.md`, no `KAKADU_ARCHIVE` |
| `.envrc.sample` | **New (optional)** — direnv recipe that regenerates GitHub netrc on reload from `gh auth token` |
| *— cross-repo: `dsp-ci-assets`* | |
| `dsp-ci-assets/scripts/publish-kakadu.sh` | **New** — publishing script; generates release notes with SHA-256, creates tag + asset via `gh release create` |
| `dsp-ci-assets/Makefile` | **Delete** — replaced by `justfile` |
| `dsp-ci-assets/justfile` | **New** — `publish-kakadu` recipe |
| `dsp-ci-assets/netdata/` | **Delete** — no longer used |
| `dsp-ci-assets/graphdb/` | **Delete** — no longer used |
| `dsp-ci-assets/README.md` | Rewrite: describe the assets, point Kakadu consumers at `kakadu/README.md` and GitHub Releases |
| `dsp-ci-assets/kakadu/README.md` | **New** — version table with SHA-256s, publishing workflow, consumer auth patterns, licensing reminder |
| `dsp-ci-assets/.gitignore` | Add `kakadu/*.zip`; keep `!kakadu/README.md` |
| `dsp-ci-assets/kakadu/*.zip` | **Delete** from working tree and from git history via `git filter-repo` (force-push, re-clone worktrees) |

---

## 9. Out of Scope

- Migrating `ext/` dependencies to nixpkgs (Phase 2)
- Removing the Zig toolchain (Phase 2)
- Removing the existing Dockerfile (Phase 2)
- macOS static binaries (not currently published)
- NixOS module for running Sipi as a service
- **dsp-api migration to tarball consumption** — the dsp-api changes to
  download tarballs and build its own `knora-sipi` image are a separate plan.
  Section 4.2 documents the target architecture and migration path, but the
  actual dsp-api work (Dockerfile, Dependencies.scala, test infra) is not
  part of this plan
- Removing the `daschswiss/sipi` Docker image from Docker Hub (keep for
  standalone users until dsp-api migration is complete)

---

## 10. Implementation Progress (2026-04-16)

**Branch:** `feature/nix-unified-build` in sipi repo (5 commits ahead of main)

### Completed
- **Section 0:** `-fno-omit-frame-pointer` added to RelWithDebInfo flags
- **Section 1:** `package.nix` fully rewritten — source filtering, dynamic version,
  correct deps, parameterized build type, separateDebugInfo, pre-fetched test deps
  (GoogleTest + ApprovalTests for sandbox compatibility), Kakadu injection via
  `kakaduArchive` parameter
- **Section 2:** `flake.nix` restructured — overlay, package variants (default/dev/release),
  devShells via `inputsFrom`, static builds (Zig-in-Nix), Docker outputs, release archives
- **Section 3:** Docker image via `dockerTools.buildLayeredImage` + streaming variant added
  to flake.nix (untested — needs Linux runner)
- **Section 4:** Static build derivations (`mkStaticBuild`, `mkReleaseArchive`) added to
  flake.nix (untested — needs Linux runner)
- **Section 5:** All CI workflows migrated from `make` to `just`, Cachix added,
  `DeterminateSystems/determinate-nix-action` replaces `cachix/install-nix-action`,
  nix-static validation job added to test.yml
- **Section 6:** justfile created with all Makefile recipes + new Nix recipes, Makefile
  and vars.mk deleted, CLAUDE.md updated
- **Section 7 (2026-04-17):** `dsp-ci-assets` migration complete.
  - PR #1 merged — `netdata/` and `graphdb/` removed
  - PR #2 merged — `scripts/publish-kakadu.sh` + `justfile` added, `Makefile` deleted,
    `README.md` rewritten, `kakadu/README.md` with all 10 SHA-256s added, `.gitignore` updated
  - All 10 Kakadu archives published as `kakadu-vX.Y` GitHub Releases
  - History rewritten with `git filter-repo --path-glob 'kakadu/*.zip' --invert-paths`
    (≈97 MB → ≈144 KB) and pushed as new branch `main`
  - Default branch renamed `master` → `main`; `master` deleted
  - v8.5 SHA-256 pinned: `c19c7579d1dee023316e7de090d9de3eb24764e349b4069e5af3a540fb644e75`
  - Smoke test passed: `gh release download kakadu-v8.5 … ; shasum` matches pin

### Build Verification (macOS arm64)
- `nix build .#default --impure` with KAKADU_ARCHIVE: **compiles successfully**
  (all ext/ deps including Kakadu and OpenSSL build correctly)
- Tests: 10/11 pass, `sipi_image_tests` fails with SIGTRAP in Nix sandbox
  (set `doCheck = false` — tests run in dev shell instead)

### Open Decision: Pure vs Impure Builds — **Resolved** ✓

**Decision (2026-04-17):** publish Kakadu archives as GitHub Release
assets on `dsp-ci-assets` and have `just kakadu-fetch` (a thin wrapper
over `gh release download`) populate `vendor/v8_5-01382N.zip`. The
flake references the file via `builtins.path { sha256 = ...; recursive = false; }`
which is pure and content-addressed at evaluation time.

Earlier in this branch a `pkgs.fetchurl` + `~/.netrc` flow was tried
but reverted in favour of the simpler `gh` CLI approach: same purity,
no dotfile editing for onboarding, clearer failure modes. CI runs
`./scripts/fetch-kakadu.sh` with `GH_TOKEN=DASCHBOT_PAT` before
`nix build`. The script also runs `git add --intent-to-add --force`
on the file so it survives Nix flake source filtering (which excludes
gitignored paths).

### Remaining Work
- [ ] Validate `nix develop` + `just nix-build && just nix-test` works
- [ ] Test Docker image output on Linux (`nix build .#docker`)
- [ ] Test static builds on Linux (`nix build .#static-amd64`)
- [ ] Investigate `sipi_image_tests` SIGTRAP in Nix sandbox
- [x] Resolve pure vs impure decision for Kakadu (see above — Sections 7 and 8)
- [x] Execute Phase 1c — `dsp-ci-assets` side (PRs #1, #2, history rewrite, default-branch rename)
- [x] Execute Phase 1c — sipi side (steps 16a–21a; 15a still pending — needs CI run to validate `DASCHBOT_PAT`)
- [x] Update `docs/src/development/building.md` with Nix-centric workflow

### Build Verification (2026-04-17, macOS arm64)
- `just kakadu-fetch && nix build .#default` succeeds — pure, no flags
- Build time: ~3:50 for the C++ build phase (cold-cache; substituted ext/ deps from prior Cachix work)
- Binary works: `./result/bin/sipi --help` runs; `strings` shows Kakadu symbols linked
- The `git add --intent-to-add --force vendor/v8_5-01382N.zip` from `fetch-kakadu.sh` is what makes the gitignored archive visible to Nix's flake source filter

### nix-static CI job — dropped on PR #558 (2026-04-17)
Attempts to CI-validate `nix build .#static-{amd64,arm64}` surfaced a
succession of Nix-sandbox-specific issues in the monolithic
ExternalProject-in-Nix build (20 deps build inside one `/build` sandbox):

- `/usr/bin/env` missing → shebang failures (OpenSSL `Configure`, curl's
  `scripts/cd2nroff`, etc.)
- `HOME=/homeless-shelter` → Zig cache directory writes fail
- glibc-shared vs musl-static library resolution conflict in libtiff's
  `tiff_mkg3states` build helper (`error: using shared libraries requires
  dynamic linking`)
- `BUILD_TESTING` wasn't a declared option, so `-DBUILD_TESTING=OFF` was
  silently ignored and the test subdirectory always pulled in a
  network-dependent `file(DOWNLOAD)` of ApprovalTests.hpp

Several of these were fixed (see commits on DEV-6265), but the remaining
problems (curl docs shebangs, libtiff-on-nix link conflict) would each
need individual patching — and the derivation is monolithic, so every
failure is a cold rebuild with no partial caching.

**Decision:** drop the `nix-static` job from CI. The existing `zig-static`
job (Alpine Docker + Zig) already validates the production static release
path. The flake targets `.#static-{amd64,arm64}` remain as dev
experiments. Re-introduce the CI job only when Phase 2 migrates individual
ext/ deps to nixpkgs-native Nix derivations, at which point each dep
becomes a separately-cacheable store path instead of an
ExternalProject-in-Nix sub-build.
- [ ] Open PR and run CI to validate all workflows
