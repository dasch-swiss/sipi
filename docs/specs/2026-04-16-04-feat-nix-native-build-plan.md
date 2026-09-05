---
title: "Sipi Nix-Native Build"
date: 2026-04-28
author: "Ivan Subotic"
status: reviewed(4)
linear: DEV-6327
related:
  - DEV-6328 (PR 0 sub-issue — drop Zig static path)
  - DEV-6329 (PR 1 sub-issue — foundation + Group 1)
  - DEV-6330 (PR 2 sub-issue — Groups 2 + 3)
  - DEV-6331 (PR 3 sub-issue — Group 4 + nix/libjpeg.nix)
  - DEV-6282 (PR 4 sub-issue — hardening + cleanup; original DEV-6282 reparented)
  - DEV-5939 (Docker-on-Nix migration — Done; this plan dissolves the per-dep ext/ tree the original DEV-5939 scope envisioned but did not deliver)
  - Plan 01 (Sipi Nix unified build)
  - Plan 02 (justfile + CI unification)
  - Plan 03 (Docker → Nix migration)
repositories:
  - name: sipi
---

# Sipi Nix-Native Build — Implementation Plan

## 1. Context

After Plans 01–03, Sipi's build is unified behind Nix derivations:
`packages.default` (Docker binary), `packages.docker{,-stream}` (production
image), `dev` / `release` / `sanitized` / `fuzz` variants. CI drives every
build through `just nix-*` recipes that wrap a single `nix build .#<variant>`
call.

Inside that derivation, however, Sipi's 22 third-party C/C++ dependencies
still build the way they did under the imperative-cmake Docker era: via
CMake `ExternalProject_Add` from `sipi/CMakeLists.txt` lines 330–351, with
sources vendored under `sipi/vendor/`. The Nix store sees one large output;
a one-character change to a `.cpp` file invalidates the whole build. The
`hardeningDisable = [ "all" ]` workaround sits at `sipi/package.nix:146`
because older autotools deps (xz, …) fail under nixpkgs default hardening.

This plan migrates Sipi to a **Nix-native build** where third-party
dependencies come from nixpkgs (consumed via `find_package`); the only
vendored exceptions are the proprietary kakadu SDK and IJG libjpeg 9f
(deferring the libjpeg-turbo migration — see §8.1). `hardeningDisable`
is dropped from the sipi binary, resolving DEV-6282 as the natural payoff.

**Living document.** The dasch-specs PR for this plan stays open for the
duration of the migration. Each sipi-side PR that lands (PR 0, 1, 2, 3, 4)
is recorded here via a checkpoint commit on the dasch-specs branch — the
"Progress" checklist below is updated, any decisions or deviations
discovered during execution are captured inline. The dasch-specs PR
merges only when all 5 sipi PRs have shipped and the greenfield shape is
reached.

## 1.1 Progress

Update via checkpoint commits as each sipi-side PR merges. A commit message convention: `checkpoint(plan): PR N — <one-line summary>`.

- [ ] **PR 0** (DEV-6328) — Drop Zig static path + test investment (image-encode approval baseline + hurl gap fills — see §6) — sipi PR [#586](https://github.com/dasch-swiss/sipi/pull/586) opened, awaiting merge
- [ ] **PR 1** (DEV-6329) — Foundation + Group 1 (8 zero-fan-in deps)
- [ ] **PR 2** (DEV-6330) — Groups 2 + 3 (cmake-native + autotools fan-in)
- [ ] **PR 3** (DEV-6331) — Group 4 + nix/libjpeg.nix
- [ ] **PR 4** (DEV-6282) — DEV-6282 hardening + final cleanup

Deviations from the plan (filled in during execution):

- **PR 0 (sipi#586) — image-encode baseline pivoted to TIFF-only outputs.** Plan §6.1 called for byte-for-byte JPEG/PNG/TIFF goldens across ~10 inputs. While capturing baselines, discovered that SipiImage's JPEG and PNG encoders (and the JP2 → any-format path) embed a fresh wall-clock timestamp into the ICC profile creation-date header on every run, making bit-exact comparison structurally impossible without upstream changes. Pivoted to **5 TIFF-output tests** (~2.3 MB of LFS-tracked goldens) covering libjpeg decode, libtiff round-trip, CMYK and CIELab colour-space paths, and rotation. TIFF outputs from JPEG and TIFF inputs preserve the source ICC verbatim, so they are deterministic. Decode-side coverage of libjpeg/libtiff/lcms2 is preserved by reading those formats as inputs; Kakadu decode bit-exactness awaits an upstream fix and is gated meanwhile by the existing e2e Rust + unit tests. Hurl gap fills landed as planned (`/health`, `/metrics`, IIIF transform, `info.json`). **Follow-up:** [DEV-6333](https://linear.app/dasch/issue/DEV-6333) ([Plan 05](./05-feat-icc-deterministic-timestamps-plan.md)) tracks adding deterministic ICC timestamps to `SipiIcc` so JPEG/PNG/JP2-decode goldens can be added back to the baseline.

### What this plan does NOT do

- **Static binaries** (`packages.static-{amd64,arm64}` /
  `release-archive-{amd64,arm64}` / `mkStaticBuild` via Zig) currently have
  **no production consumer**. Production runs the dynamic Docker binary.
  This plan **deletes** the unused Zig path. A future `pkgsStatic` /
  `pkgsCross.aarch64-multiplatform-musl` build is a separate plan, deferred
  until a real Docker-image-static-swap initiative drives it.
- **`shttps` separation** (the strangler-fig target per
  `sipi/CONTEXT-MAP.md`) is out of scope; tracked separately.

## 2. Goals & Non-Goals

### Goals

1. **Greenfield Nix-native shape.** Sipi's CMakeLists.txt uses
   `find_package` against nixpkgs deps. No `ext/` directory. No `vendor/`.
   ~80-line top-level `CMakeLists.txt`.
2. **Resolve DEV-6282.** Drop `hardeningDisable = [ "all" ]` from the sipi
   derivation. `checksec result/bin/sipi` reports stack-protector-strong,
   fortify=2, RELRO, BIND_NOW. `.#sanitized` and `.#fuzz` keep their
   current hardening posture (sanitizer/fuzzer instrumentation conflicts
   with hardening).
3. **Kakadu and libjpeg as the only vendored exceptions.** Promote
   `ext/kakadu/`'s build into `nix/kakadu.nix` (consuming the existing
   FOD archive) and create `nix/libjpeg.nix` (IJG 9f via fetchurl FOD;
   defers libjpeg-turbo migration — see §8.1). The other 20 ext deps
   come from nixpkgs.
4. **Delete dead code.** Remove `mkStaticBuild`, `cmake/zig-toolchain.cmake`,
   `cmake/dependencies.cmake`, `scripts/vendor.sh`, `vendor-*` justfile
   recipes, `cmake-openssl.patch`, `CMakeLists.poppler.patch`, the entire
   `ext/` tree (after migration), and `vendor/`.
5. **Preserve every build-completeness invariant.** macOS darwin-aarch64,
   linux-x86_64, linux-aarch64. CI green throughout. No regression in
   image-pipeline output bit-exactness.

### Non-goals

- pkgsStatic-based static binaries (deferred to a future plan)
- shttps separation (tracked separately)
- Migrating dev-shell inner-loop semantics (`nix develop` + `cmake --build build`)
- `find_package` migrations after PR 4 lands (the plan finishes with
  20 deps from nixpkgs + kakadu and libjpeg vendored; further
  opportunistic optimization is post-plan)

## 3. Greenfield Target

The destination shape, presented up front so every PR's intermediate
state can be evaluated against it.

### 3.1 Source tree

```
sipi/
├── flake.nix                 # ~150 lines: variants, dockerTools, dev shells
├── nix/
│   ├── sipi.nix              # the derivation: callPackage takes deps, returns the binary
│   ├── kakadu.nix            # vendored proprietary JPEG2000 SDK
│   ├── kakadu-archive.nix    # FOD against dsp-ci-assets (lifted from flake.nix)
│   └── libjpeg.nix           # vendored IJG 9f (fetchurl FOD against ijg.org); avoids libjpeg-turbo ABI swap
├── CMakeLists.txt            # ~80 lines: project + find_package + add_executable + tests
├── src/, include/, shttps/   # unchanged
├── test/                     # gains approval-test image-encode bit-exactness suite
├── fuzz/, config/, scripts/  # unchanged
├── server/                   # unchanged
├── patches/
│   └── kakadu-makefile-*.patch  # the only patches that survive
└── docs/
```

### 3.2 What's gone

| Removed | Reason |
|---|---|
| `ext/` (the whole tree, ~22 subdirectories) | All non-kakadu deps come from nixpkgs; kakadu lives in `nix/kakadu.nix` |
| `vendor/` (21 archives, ~hundreds of MiB) | Sources fetched from nixpkgs; FOD pinning for kakadu and libjpeg |
| `cmake/dependencies.cmake` | URLs/sha256s no longer in-tree |
| `cmake/zig-toolchain.cmake` | Zig static path deleted |
| `package.nix` (top-level) | Moved to `nix/sipi.nix` |
| `scripts/vendor.sh` | No vendoring left |
| `patches/cmake-openssl.patch` | Cosmetic; openssl from nixpkgs builds clean |
| `patches/CMakeLists.poppler.patch` | Already dead (poppler not added in CMakeLists) |
| `mkStaticBuild`, `mkReleaseArchive` (in flake.nix) | Static path deleted |
| `nix-build-static-*`, `nix-build-release-archive-*`, `vendor-*`, `kakadu-fetch` (justfile recipes) | Underlying outputs/scripts gone |
| Static-publication steps in `publish.yml` | No artifact to publish |
| `STATIC IMPORTED` declarations in `CMakeLists.txt` | Replaced by `find_package` + modern imported targets |
| Hand-ordered monolithic `target_link_libraries` line | Replaced by `INTERFACE_LINK_LIBRARIES` carried by imported targets |
| `hardeningDisable = [ "all" ]` in `nix/sipi.nix` | DEV-6282 |

### 3.3 Final `CMakeLists.txt` shape

```cmake
cmake_minimum_required(VERSION 3.28)
project(sipi VERSION ${EXT_PROVIDED_VERSION} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
find_package(Threads REQUIRED)

# nixpkgs deps via CMake bundled Find modules
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)
find_package(LibLZMA REQUIRED)
find_package(TIFF REQUIRED)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)                       # bundled FindJPEG locates vendored IJG 9f via nix/libjpeg.nix
find_package(SQLite3 REQUIRED)
find_package(EXPAT REQUIRED)

# nixpkgs deps via upstream Config.cmake (CONFIG required to skip CMake's bundled FindXxx)
find_package(zstd CONFIG REQUIRED)
find_package(WebP CONFIG REQUIRED)                # WebPConfig.cmake; requires nixpkgs ≥ 2024-10
find_package(exiv2 CONFIG REQUIRED)               # lowercase — matches upstream exiv2Config.cmake
find_package(prometheus-cpp CONFIG REQUIRED)      # ships prometheus-cpp-config.cmake
find_package(sentry CONFIG REQUIRED)              # sentry-native; pulls in CURL + pthread transitively

# pkg-config-only deps (no upstream Config.cmake; nixpkgs ships only .pc files)
include(FindPkgConfig)
pkg_check_modules(lcms2    REQUIRED IMPORTED_TARGET GLOBAL lcms2)
pkg_check_modules(jansson  REQUIRED IMPORTED_TARGET GLOBAL jansson)   # nixpkgs jansson built via autotools — no Config.cmake
pkg_check_modules(libmagic REQUIRED IMPORTED_TARGET GLOBAL libmagic)  # if libmagic.pc is missing on a nixpkgs version, write a project-local Findmagic.cmake fallback

# Lua needs a manual imported target (FindLua doesn't create one)
find_package(Lua 5.3 EXACT REQUIRED)              # 5.3 EXACT — sipi bindings are 5.3-API-locked
add_library(Lua::Lua UNKNOWN IMPORTED)
set_target_properties(Lua::Lua PROPERTIES
  IMPORTED_LOCATION "${LUA_LIBRARIES}"
  INTERFACE_INCLUDE_DIRECTORIES "${LUA_INCLUDE_DIR}"
)

# Our own derivation (kakaduConfig.cmake generated by nix/kakadu.nix)
find_package(kakadu CONFIG REQUIRED)

add_executable(sipi
  # SIPI sources (preserve from current CMakeLists.txt:467-520)
  src/sipi.cpp
  src/SipiConf.cpp src/SipiRateLimiter.cpp src/SipiMemoryBudget.cpp
  src/SipiError.cpp src/SipiImage.cpp src/SipiHttpServer.cpp
  src/SipiCache.cpp src/SipiMetrics.cpp src/SipiLua.cpp
  src/SipiCommon.cpp src/SipiFilenameHash.cpp src/SipiReport.cpp
  src/Logger.cpp
  src/metadata/SipiIcc.cpp src/metadata/SipiXmp.cpp
  src/metadata/SipiIptc.cpp src/metadata/SipiExif.cpp
  src/metadata/SipiEssentials.cpp
  src/formats/SipiIOTiff.cpp src/formats/SipiIOJ2k.cpp
  src/formats/SipiIOJpeg.cpp src/formats/SipiIOPng.cpp
  src/iiifparser/SipiRotation.cpp src/iiifparser/SipiQualityFormat.cpp
  src/iiifparser/SipiRegion.cpp src/iiifparser/SipiSize.cpp
  src/iiifparser/SipiDecodeDims.cpp src/iiifparser/SipiIdentifier.cpp
  src/handlers/iiif_handler.cpp
  # shttps sources (inlined; shttps/CMakeLists.txt is dormant)
  shttps/Error.cpp shttps/Hash.cpp shttps/SockStream.cpp
  shttps/ChunkReader.cpp shttps/Connection.cpp
  shttps/LuaServer.cpp shttps/LuaSqlite.cpp
  shttps/Parsing.cpp shttps/ThreadControl.cpp
  shttps/SocketControl.cpp shttps/Server.cpp shttps/jwt.c
)
# Note: current CMakeLists.txt has a duplicate `src/handlers/iiif_handler.cpp`
# entry (lines 490 and 518); fix in PR 1's CMakeLists rewrite.
target_compile_features(sipi PRIVATE cxx_std_23)
target_include_directories(sipi PRIVATE include)
target_link_libraries(sipi PRIVATE
  Threads::Threads
  OpenSSL::SSL OpenSSL::Crypto CURL::libcurl
  ZLIB::ZLIB BZip2::BZip2 LibLZMA::LibLZMA zstd::libzstd_shared
  TIFF::TIFF PNG::PNG JPEG::JPEG WebP::webp
  Exiv2::exiv2lib SQLite::SQLite3 EXPAT::EXPAT
  PkgConfig::lcms2 PkgConfig::jansson PkgConfig::libmagic
  Lua::Lua
  prometheus-cpp::pull            # transitively pulls prometheus-cpp::core
  sentry::sentry                  # transitively pulls CURL + pthread
  kakadu::kdu_aux
)

# iconv: macOS-only system lib (preserve current CMakeLists.txt:278-280 logic)
if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  target_link_libraries(sipi PRIVATE iconv "-framework CoreFoundation" "-framework SystemConfiguration")
endif()

install(TARGETS sipi DESTINATION bin)
install(FILES config/sipi.config.lua config/sipi.init.lua DESTINATION share/sipi/config)
install(FILES server/test.html DESTINATION share/sipi/server)
install(FILES scripts/test_functions.lua scripts/send_response.lua DESTINATION share/sipi/scripts)

if(BUILD_TESTING)
  add_subdirectory(test)
endif()
if(SIPI_ENABLE_FUZZ)
  add_subdirectory(fuzz)
endif()
```

### 3.4 Final `flake.nix` outputs

```
packages.default              # pkgs.sipi (RelWithDebInfo, glibc dynamic, hardened)
packages.dev                  # Debug + coverage
packages.release              # Release, unstripped
packages.sanitized            # Debug + ASan/UBSan (hardeningDisable kept)
packages.fuzz                 # libFuzzer harness (hardeningDisable kept)
packages.docker               # dockerTools.buildLayeredImage
packages.docker-stream        # dockerTools.streamLayeredImage
packages.kakadu               # callPackage ./nix/kakadu.nix (default stdenv)
packages.kakaduGccDarwin      # pkgs.kakadu.override { stdenv = pkgs.gcc14Stdenv; }; verification convenience for the Mac-arm-64-gcc patch path
packages.kakaduArchive        # FOD (existing)
packages.libjpeg              # callPackage ./nix/libjpeg.nix (vendored IJG 9f)
packages.sipi-debug           # passthrough .debug output
```

Removed from current outputs: `static-{amd64,arm64}`,
`release-archive-{amd64,arm64}`. (PR 0.)

### 3.5 `nix/kakadu.nix` shape

The kakadu derivation produces `$out/{lib,include}` plus a CMake config so
sipi can `find_package(kakadu)`:

```nix
{ stdenv, lib, kakaduArchive, unzip }:

let
  # Derive kakadu's KDU_EXEC_PLATFORM from nixpkgs stdenv at evaluation time.
  # Sipi's flake.nix forces pkgs.sipi to libcxxStdenv on macOS; the gcc
  # devShell uses gcc14Stdenv. nix/kakadu.nix is callPackaged with the
  # caller's stdenv, so we honour whatever the caller passes.
  kduPlatform =
    if stdenv.hostPlatform.isDarwin && stdenv.hostPlatform.isAarch64
      then if stdenv.cc.isClang then "Mac-arm-64-clang" else "Mac-arm-64-gcc"
    else if stdenv.hostPlatform.isLinux && stdenv.hostPlatform.isx86_64
      then "Linux-x86-64-gcc"
    else if stdenv.hostPlatform.isLinux && stdenv.hostPlatform.isAarch64
      then "Linux-arm-64-gcc"
    else throw "kakadu: unsupported host platform ${stdenv.hostPlatform.system}";

  kduArch =
    if stdenv.hostPlatform.isDarwin then "Mac-arm-64"
    else if stdenv.hostPlatform.isx86_64 then "Linux-x86-64"
    else "Linux-arm-64";

  # Mac-arm-64-gcc is the only target that needs the 3 patches (clang++ → g++).
  # Other targets either use clang (no patch) or are Linux gcc (no patch).
  needsGccPatches = kduPlatform == "Mac-arm-64-gcc";
in

stdenv.mkDerivation (finalAttrs: {
  pname = "kakadu";
  version = "8.5";
  src = kakaduArchive;

  nativeBuildInputs = [ unzip ];

  patches = lib.optionals needsGccPatches [
    ../patches/kakadu-makefile-apps.patch
    ../patches/kakadu-makefile-coresys.patch
    ../patches/kakadu-makefile-managed.patch
  ];

  unpackPhase = ''
    runHook preUnpack
    unzip -q $src -d .
    # Auto-detect the unzipped directory rather than hard-coding the version.
    cd "$(find . -maxdepth 1 -type d -name 'v*' -print -quit)"
    runHook postUnpack
  '';

  buildPhase = ''
    runHook preBuild
    cd make
    make -j1 -f Makefile-${kduPlatform} EXEC_PLATFORM=${kduPlatform} all_but_jni
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/lib $out/include $out/lib/cmake/kakadu
    cp ../lib/${kduArch}/libkdu*.a $out/lib/
    cp -r ../managed/all_includes/* $out/include/

    # Generate kakaduConfig.cmake so find_package(kakadu) works.
    # The single-quoted heredoc preserves ${CMAKE_CURRENT_LIST_DIR}
    # literally so CMake (not Bash) evaluates it at find_package time.
    cat > $out/lib/cmake/kakadu/kakaduConfig.cmake <<'EOF'
    add_library(kakadu::kdu STATIC IMPORTED)
    set_target_properties(kakadu::kdu PROPERTIES
      IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/../../libkdu.a"
      INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../../include"
    )
    add_library(kakadu::kdu_aux STATIC IMPORTED)
    set_target_properties(kakadu::kdu_aux PROPERTIES
      IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/../../libkdu_aux.a"
      INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../../include"
      INTERFACE_LINK_LIBRARIES kakadu::kdu
    )
    EOF
    runHook postInstall
  '';

  hardeningDisable = [ "all" ];

  meta = with lib; {
    description = "Kakadu JPEG2000 SDK (proprietary)";
    homepage = "https://kakadusoftware.com/";
    license = licenses.unfree;        # critical: forces downstream consumers to opt in via NIXPKGS_ALLOW_UNFREE
    platforms = [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" ];
    sourceProvenance = with sourceTypes; [ binaryNativeCode ];
  };
})
```

**Note on `pkgs.libjpeg` overlay shadowing** (relevant to `nix/sipi.nix`'s
`buildInputs`): we call `pkgs.libjpeg = prev.callPackage ./nix/libjpeg.nix
{ };` in the overlay, which shadows nixpkgs' `libjpeg` (libjpeg-turbo).
Document this with an inline comment in `flake.nix` so future maintainers
don't `pkgs.libjpeg` expecting libjpeg-turbo.

## 4. Current State Delta

The 22 ext/* deps and where each goes in the target state:

| ext dep | Build system | Target | Group |
|---|---|---|---|
| zlib | autotools | nixpkgs `zlib` → `ZLIB::ZLIB` | 1 |
| bzip2 | makefile | nixpkgs `bzip2` → `BZip2::BZip2` | 1 |
| xz | autotools | nixpkgs `xz` → `LibLZMA::LibLZMA` | 1 |
| zstd | cmake | nixpkgs `zstd` → `zstd::libzstd_shared` (matches the dynamic-link plan) | 1 |
| jansson | autotools | nixpkgs `jansson` → `PkgConfig::jansson` (no Config.cmake; nixpkgs jansson is autotools-only) | 1 |
| expat | autotools | nixpkgs `expat` → `EXPAT::EXPAT` | 1 |
| lcms2 | autotools | nixpkgs `lcms2` → `PkgConfig::lcms2` (no Config.cmake upstream) | 1 |
| sqlite3 | autotools | nixpkgs `sqlite` → `SQLite::SQLite3` | 1 |
| png | cmake | nixpkgs `libpng` → `PNG::PNG` | 2 |
| tiff | cmake | nixpkgs `libtiff` → `TIFF::TIFF` | 2 |
| webp | cmake | nixpkgs `libwebp` → `WebP::webp` (requires nixpkgs ≥ 2024-10 for WebPConfig.cmake) | 2 |
| exiv2 | cmake | nixpkgs `exiv2` → `Exiv2::exiv2lib` | 2 |
| openssl | perl Configure | nixpkgs `openssl` → `OpenSSL::SSL`, `OpenSSL::Crypto` | 3 |
| curl | autotools | nixpkgs `curl` → `CURL::libcurl` | 3 |
| libmagic (file) | autotools | nixpkgs `file` → `PkgConfig::libmagic` (write project-local Findmagic.cmake fallback if .pc missing) | 3 |
| jpeg (IJG 9f) | autotools | `nix/libjpeg.nix` (vendored IJG 9f, fetchurl FOD) → `JPEG::JPEG` | 4 |
| jbigkit | makefile | nixpkgs `jbigkit` (or bundled w/ libtiff) | 4 |
| lua | makefile | nixpkgs `lua5_3` → manually-defined `Lua::Lua` imported target (FindLua doesn't create one) | 4 |
| luarocks | shell | nixpkgs `luarocks` (build-time only) | 4 |
| sentry-native | cmake | nixpkgs `sentry-native` → `sentry::sentry` (Config.cmake; pulls CURL + pthread transitively) | 4 |
| prometheus-cpp | cmake | nixpkgs `prometheus-cpp` → `prometheus-cpp::pull` (Config.cmake; pulls `::core` transitively) | 4 |
| kakadu | makefile (proprietary) | `nix/kakadu.nix` → `kakadu::kdu_aux` | special |

**Group 1** (8 deps, low ABI risk): basic compression / serialization libs that nixpkgs has at compatible versions. Approval-test gate.

**Group 2** (4 deps, medium risk): cmake-native image-codec libs. Output bit-exactness verified by approval tests.

**Group 3** (3 deps, medium risk): autotools fan-in — openssl version compatibility is the risk surface; curl HTTP behavior is the second.

**Group 4** (6 entries in the table — 5 migrate to nixpkgs, 1 stays vendored):
- **libjpeg stays vendored as `nix/libjpeg.nix`** — IJG 9f from a `fetchurl` FOD against `ijg.org`. Avoids the libjpeg-turbo ABI swap entirely. The libjpeg-turbo migration is deferred to a future plan (§10).
- **lua 5.3.5 → 5.3.x point release in nixpkgs**: should be ABI-clean within 5.3.x; verify via Sipi's `scripts/*.lua` paths.
- **prometheus-cpp / sentry-native / luarocks / jbigkit**: version-compatibility verification per dep. (jbigkit is typically pulled in transitively by libtiff in nixpkgs; verify before assuming.) If any breaks, fall back to a `nix/<dep>.nix` vendored derivation (same pattern as libjpeg/kakadu).

## 5. Migration Strategy

**Outside-in, deletion-driven.** Each PR is defined by what it removes. The greenfield target has no `ext/` and no `vendor/`; the migration moves toward that state by deleting one `ext/<dep>/` + one `vendor/<archive>` + one block of `STATIC IMPORTED` declarations per dep, replacing them with a `find_package` line. Zig is dropped in PR 0 so subsequent PRs can delete `ext/<dep>/CMakeLists.txt` files cleanly without dual-consumption flag gating.

### 5.1 `flake.lock` strategy

**`flake.lock` is frozen for the duration of this migration.** PR 0
establishes the baseline; PRs 1–4 do **not** run `nix flake update`. Any
nixpkgs input bump must be its own PR with the bump as the only change.
PR 4's merge may optionally include a deliberate `nix flake update` to
get a fresh post-migration pin.

### 5.2 Cross-PR rollback strategy

Per-PR `git revert` is the per-PR rollback (each PR section's
"Rollback"). The trickier case — **PR N+1 ships, then a prod incident
attributable to PR N surfaces** — is handled as follows:

- **PR N already merged + observed-clean for ≥1 weekly prod cut:**
  rollback is a forward fix (new PR), not a revert. The risk of stacking
  reverts on a 4-PR migration is substantial (each PR mutates
  `cmake/dependencies.cmake`, `target_link_libraries`, and `nix/sipi.nix`
  buildInputs).
- **PR N merged < 1 weekly cycle ago (i.e., PR N+1 hasn't shipped yet):**
  revert PR N before opening PR N+1. The prod-state hasn't picked up the
  change yet anyway.
- **Mid-migration "this PR sequence isn't going to land cleanly":**
  abort, revert all merged PRs in reverse order back to PR 0's state
  (Zig deletion stays — that's load-bearing-irreversible). Reset to a
  known-good commit on `main`.

### 5.3 Rollout comms

PR 1, PR 2, PR 4 each have a structurally large delete footprint
(vendored archives + ext subdirs disappear). Teammates on stale
checkouts running `just nix-build` after `git pull` will see confusing
errors during the transition. **Before merging each of those PRs:**

- Post in `#engineering` ("Sipi build refactor PR N merging — pull main
  + `git clean -fdx` before next dev session")
- Pin in `#sipi-dev` for 48h post-merge

PR 0 (deletes Zig) and PR 3 (smaller delta) don't need the comms step.

### 5.4 dasch-specs PR as living tracker

This plan file lives in `dasch-specs`. The dasch-specs PR for the plan
**stays open** for the duration of the migration. The plan is a living
progress tracker:

- **One commit per sipi-PR-merge.** When PR N (in sipi) merges to sipi
  `main`, push a checkpoint commit on the dasch-specs branch:
  `checkpoint(plan): PR N — <summary>`. The commit ticks the corresponding
  box in §1.1 Progress and notes any deviations from the plan
  (e.g., "lua5_3 vendored as `nix/lua5_3.nix` because of ABI drift on
  `string.pack`").
- **Sipi PR descriptions link the dasch-specs branch** (not `main`,
  since the plan isn't merged yet). The branch URL is stable for the
  duration of the migration.
- **Final merge.** When PR 4 ships and the greenfield shape is reached,
  the dasch-specs PR merges to `main`. This makes the executed plan the
  historical record of the migration.

This pattern keeps the plan and execution in lock-step. Anyone reading
the dasch-specs PR mid-migration sees both the original design and which
phases have landed.

### 5.5 Cachix push configuration

Reuse DaSCH's existing Cachix token (no new secret). Concrete wiring:

- **Cache name:** existing DaSCH cache (whichever is configured today for
  `pkgs.sipi` / `packages.docker-stream`). Verify in
  `.github/workflows/test.yml` or `publish.yml` Cachix step.
- **Push targets:** add `packages.kakadu` (PR 1) and `packages.libjpeg`
  (PR 3) to the Cachix push list. The existing Cachix step is presumably
  `cachix push <cache> result*` — extend so it also pushes `result-*`
  symlinks from `nix build .#kakadu` / `nix build .#libjpeg`.
- **CI job placement:** kakadu pushes from the Linux-x86_64 + arm64 jobs
  (matches existing matrix). darwin-aarch64 builds happen locally only
  per `sipi/CLAUDE.md` invariant; pushing from local is optional.
- **Token secret name:** existing repo secret (`CACHIX_AUTH_TOKEN` or
  whatever the current workflow references). Don't create a new one.

PR 1 adds the push step for `packages.kakadu`; PR 3 extends to
`packages.libjpeg`.

## 6. Test Investment (lands in PR 0)

Pre-investment that hedges against bundling risk. ~1 day of work; pays
off across all 5 PRs. **Lands in PR 0** alongside the Zig deletion.

**Why PR 0:** the image-encode approval goldens must be captured against
the unmigrated build. Folding into PR 1 would capture goldens against
the post-Group-1-migration build, defeating the purpose. PR 0 deletes
Zig but leaves the dynamic build's output bytes unchanged, so the
baseline is clean. PR 0 is also risk-zero (no production consumer for
Zig outputs), so the test-investment additions don't change the risk
profile.

### 6.1 Image-encode bit-exactness approval suite

Goal: capture today's byte-for-byte JPEG/TIFF/WebP/PNG output for a fixed
set of representative input images, so every PR can be evaluated against
an objective regression gate (and so the deferred libjpeg-turbo migration
in a future plan has a baseline waiting when it lands).

Add under `test/_test_data/images/encode_baseline/` (matches existing
`test/_test_data/images/` convention used by `metadata_golden_test.cpp`):

```
test/_test_data/images/encode_baseline/
  ├── color_chart_8bit.jpg     # JPEG decode + scale + JPEG encode (typical path)
  ├── color_chart_16bit.tif    # 16-bit TIFF decode + scale + JPEG encode (depth conversion)
  ├── icc_embedded.jpg         # ICC profile preservation through transcoding
  ├── exif_metadata.jpg        # EXIF metadata round-trip
  ├── cmyk_input.tif           # CMYK → sRGB conversion via lcms2
  ├── alpha.png                # PNG with alpha channel preservation
  ├── alpha.webp               # WebP alpha (lossy + lossless)
  ├── multipage.tif            # multi-page TIFF (libtiff directory iteration)
  ├── lzw_compressed.tif       # LZW-compressed TIFF (legacy decoder path)
  └── jpeg_in_tiff.tif         # JPEG-in-TIFF (libtiff↔libjpeg interop)
```

Approved goldens land under `test/approval/approval_tests/` (existing
`useApprovalsSubdirectory("approval_tests")` configuration applies).

For each input, run sipi against ~5 IIIF parameter combinations (full
size, downscaled, region cropped, rotated, format-converted), capture
output bytes. ApprovalTests records first-run as the golden; subsequent
PRs re-run and diff against it.

Existing infrastructure: `test/approval/metadata_golden_test.cpp` (loads
images from `../../../test/_test_data/images/` via
`useApprovalsSubdirectory("approval_tests")`) is the right template — copy
its pattern. (Don't follow `iiif_parse_approval_test.cpp`, which is a
string-only test for the URI parser and doesn't load images.) Add
`test/approval/image_encode_baseline_test.cpp` and drop input images
under `test/_test_data/images/encode_baseline/`. Approved goldens live
under `test/approval/approval_tests/` next to the test source.

**Acceptance:** the baseline test passes on `pkgs.sipi` (today's build);
every subsequent PR re-runs it as a regression gate.

**What "passes" means:** byte-for-byte identical to the captured golden by
default (ApprovalTests' `assertEqualBinary` mode). If a migration produces
genuinely different bytes (e.g., a libpng version bump changing default
zlib compression level), re-approval is allowed under this procedure:
(a) attach diff images + a maintainer-signed-off note in the PR
description showing the new outputs are visually equivalent and
IIIF-compliant, (b) the re-approved goldens commit in the same PR,
(c) add an entry to `test/approval/CHANGELOG.approval.md` (new file)
explaining the drift.

### 6.2 Hurl gap analysis

Current `test/hurl/*.hurl` covers Lua endpoints, file access permissions,
SQLite API, and video sidecar paths. **Confirmed gaps** (against routes
registered in `src/SipiHttpServer.cpp:2258-2262`):

- **`GET /health`** — used by Docker `HEALTHCHECK`; no hurl test
- **`GET /metrics`** — Prometheus exposer; no hurl test (relevant to PR 3)
- **`GET /favicon.ico`** — minor; nice-to-have
- **IIIF transformation paths** (`/{id}/{region}/{size}/{rotation}/{quality}.{format}`) — covered only by Rust e2e (`test/e2e-rust/`); add hurl coverage for at least the canonical-URL redirect + format-conversion contracts
- **`info.json`**, **`knora.json`**, **`file`** sub-paths — same; e2e only

Add hurl tests for at minimum: `/health`, `/metrics`, one canonical IIIF
transformation request, one `info.json` request. Target: every
`add_route(...)` call in `SipiHttpServer.cpp:2258-2262` has at least one
hurl assertion.

### 6.3 Perf benchmarking — not in this plan

Perf regressions are out of scope for synthetic benchmarking. DSP Grafana monitors prod sipi (request latency, RSS); the image-encode approval suite catches the high-likelihood correctness failure mode; if a regression surfaces in prod, build the benchmark then.

## 7. PR-by-PR Execution

### PR 0 — Drop Zig static path + test investment

**Motivation:** Two preparation moves bundled. (1) Zig consumes `ext/<dep>/CMakeLists.txt` via `mkStaticBuild`'s ExternalProject; deleting it first lets every subsequent PR delete those CMakeLists files cleanly. (2) Capture the image-encode approval baseline against the unmigrated build so PRs 1–4 have a fixed correctness gate, plus fill hurl coverage gaps. Both fit naturally in PR 0 because PR 0 is risk-zero (no prod consumer) and leaves the dynamic build's output bytes unchanged.

**Goal:** Delete the unused Zig-based static-build infrastructure; add image-encode approval baseline + hurl gap fills.

**Effort:** ~2 days (Zig deletion: ~1; test investment: ~1 — see §6).

**Risk:** None. No production consumer (`project_sipi_static_archives_unused.md`).

**Files added (test investment, see §6):**
- `test/approval/image_encode_baseline_test.cpp` — image-encode bit-exactness baseline (§6.1)
- `test/_test_data/images/encode_baseline/*.{jpg,tif,png,webp}` — ~10 representative input images (§6.1)
- `test/approval/CHANGELOG.approval.md` — re-approval audit log (created empty; populated only when goldens drift)
- New hurl tests: `test/hurl/health.hurl`, `test/hurl/metrics.hurl`, `test/hurl/iiif_transform.hurl`, `test/hurl/info_json.hurl` (§6.2)

**Files deleted (Zig):**
- `cmake/zig-toolchain.cmake`
- (none others under `cmake/`)

**Files modified:**
- `flake.nix`:
  - Delete `mkStaticBuild` (lines ~134–202)
  - Delete `mkReleaseArchive` (lines ~205–224)
  - Delete `static-amd64`, `static-arm64`, `release-archive-amd64`, `release-archive-arm64` outputs (in `pkgs.lib.optionalAttrs isLinux { ... }` block)
- `cmake/dependencies.cmake`:
  - Delete `ZIG_EP_*` variable definitions (`ZIG_EP_CMAKE_ARGS`, `ZIG_EP_CFLAGS`, `ZIG_EP_CXXFLAGS`)
- `ext/<dep>/CMakeLists.txt` × 22:
  - Delete `${ZIG_EP_CMAKE_ARGS}` references in each `ExternalProject_Add` block
  - Delete Zig-specific patch `sed` invocations
- `justfile`:
  - Delete `nix-build-static-amd64`, `nix-build-static-arm64`, `nix-build-release-archive-amd64`, `nix-build-release-archive-arm64`, `nix-docker-extract-debug` (if it depends on static)
- `.github/workflows/publish.yml`:
  - Delete static-publication job/steps
- `docs/src/development/building.md`, `docs/src/development/ci.md`:
  - Delete Zig sections; update build matrix
- Dev shells in `flake.nix`:
  - Remove `zig` from package lists if explicitly listed (currently implicit via inputsFrom; verify and remove dead Zig refs)

**Verification:**

```bash
just nix-build              # .#dev still builds
just nix-build-default      # .#default still builds
just nix-build-release      # .#release still builds
just nix-build-sanitized    # .#sanitized still builds
just nix-build-fuzz         # .#fuzz still builds
just nix-docker-build       # docker still builds
just nix-coverage           # coverage XML produced
just rust-test-e2e          # e2e tests pass
just hurl-test              # contract tests pass

# Multi-platform
nix build .#packages.x86_64-linux.default     # via native-linux-builder
nix build .#packages.aarch64-linux.default    # via native-linux-builder

# These should fail (now-removed):
! nix build .#static-amd64 2>/dev/null
! nix build .#release-archive-amd64 2>/dev/null

# CI surface
nix flake check
```

**Acceptance:**
- All non-static variants build on macOS-aarch64 + linux-{x86_64,aarch64}
- CI `test.yml` + `sanitizer.yml` + `publish.yml` (now without static steps) green
- `just --list` shows no `*-static-*` or `*-release-archive-*` recipes
- `git grep -i zig` returns nothing in `cmake/`, `flake.nix`, `justfile`, `package.nix`, `ext/`
- **Baseline goldens captured against the unmigrated build:** `test/approval/image_encode_baseline_test.cpp` passes on `pkgs.sipi`; first-run goldens committed under `test/approval/approval_tests/`
- New hurl tests pass against today's build (`/health`, `/metrics`, IIIF transform, `info.json`)

**Rollback:** Single `git revert`.

---

### PR 1 — Foundation + Group 1 (8 zero-fan-in deps)

**Motivation:** Establish the Nix-native shape with the lowest-risk subset. Foundation (move package.nix → nix/, kakadu derivation + Cachix push) is pure refactor; Group 1 deps have small ABI surface so they're the safest first nixpkgs swap. Sets the pattern for PRs 2–3 to follow.

**Goal:** Move to `nix/`-rooted layout, promote kakadu to its own
derivation, and migrate the 8 ABI-stable deps to nixpkgs.

**Effort:** ~5 days (Foundation: ~2; Kakadu derivation: ~1; Group 1 deps: ~2).

**Risk:** Low. Foundation is pure refactor; Group 1 deps have small ABI surface.

**Files added:**
- `nix/sipi.nix` (lifted from current `package.nix`, slimmed to consume kakadu via callPackage)
- `nix/kakadu.nix` (new derivation; produces `$out/{lib,include}` + `kakaduConfig.cmake`)
- `nix/kakadu-archive.nix` (extracted from `flake.nix`'s `mkKakaduArchive`)

**Flake outputs added in this PR:**
- `packages.kakadu` — default stdenv (libcxx-clang on darwin, gcc14 on Linux)
- `packages.kakaduArchive` — FOD archive
- `packages.kakaduGccDarwin = pkgs.kakadu.override { stdenv = pkgs.gcc14Stdenv; }` — exercises the Mac-arm-64-gcc patch path (only meaningful on darwin)

**Files deleted:**
- `package.nix` (top-level; moved to `nix/sipi.nix`)
- `ext/zlib/CMakeLists.txt`, `ext/bzip2/CMakeLists.txt`, `ext/xz/CMakeLists.txt`, `ext/zstd/CMakeLists.txt`, `ext/jansson/CMakeLists.txt`, `ext/expat/CMakeLists.txt`, `ext/lcms2/CMakeLists.txt`, `ext/sqlite3/CMakeLists.txt`
- `vendor/zlib-1.3.2.tar.gz`, `vendor/bzip2-1.0.8.tar.gz`, `vendor/xz-5.2.5.tar.gz`, `vendor/zstd-1.5.7.tar.gz`, `vendor/jansson-2.13.1.tar.gz`, `vendor/expat-2.5.0.tar.bz2`, `vendor/lcms2-2.16.tar.gz`, `vendor/sqlite-autoconf-3450200.tar.gz`

**Files modified:**
- `flake.nix`:
  - Import `nix/sipi.nix`, `nix/kakadu.nix`, `nix/kakadu-archive.nix`
  - Update overlay: `kakadu = prev.callPackage ./nix/kakadu.nix { kakaduArchive = final.kakaduArchive; }`
  - Update overlay: `sipi = prev.callPackage ./nix/sipi.nix { kakadu = final.kakadu; ... }`
  - Add `kakadu` and `kakaduArchive` to `packages.<system>` outputs
- `CMakeLists.txt`:
  - Add `find_package` calls for the 8 Group 1 deps after the existing `find_package(Threads REQUIRED)` (line 265)
  - Delete `add_subdirectory(${COMMON_EXT}/<dep>)` lines 330–351 for the 8 migrated deps (keep the others for now)
  - Delete `STATIC IMPORTED` blocks for the 8 deps (need to identify per-dep block locations)
  - Update `target_link_libraries(${PROJECT_NAME} ${LIBS} …)` (line 530): replace `xz`→`LibLZMA::LibLZMA`, `z`→`ZLIB::ZLIB`, `bzip2`→`BZip2::BZip2`, `zstd`→`zstd::libzstd_shared`, `jansson`→`PkgConfig::jansson`, `expat`→`EXPAT::EXPAT`, `lcms2`→`PkgConfig::lcms2`, `sqlite3`→`SQLite::SQLite3` (per the §3.3 final shape and the best-practices research findings on which deps ship Config.cmake vs only pkg-config in nixpkgs)
- `cmake/dependencies.cmake`:
  - Delete `set(DEP_<NAME>_*)` blocks for the 8 migrated deps
- `scripts/vendor.sh`:
  - Update dep list (remove the 8); leave the script for the remaining 13
- `justfile`:
  - `vendor-*` recipes still serve remaining deps; no recipe deletions yet
- `nix/sipi.nix`:
  - `buildInputs = [ kakadu zlib bzip2 xz zstd jansson expat lcms2 sqlite ... readline iconv perl ]`
  - Keep `hardeningDisable = [ "all" ]` (still needed; remaining ext deps are unmigrated)
  - Keep cmake hooks, separateDebugInfo, postFixup, etc.

**Test investment:** **landed in PR 0** (see §6). PR 1 consumes the
baselines as regression gates; this PR doesn't add new test
infrastructure beyond what each migration touches.

**Verification:**

```bash
# Standard build matrix
just nix-build && just nix-build-default && just nix-build-release
just nix-build-sanitized && just nix-build-fuzz
just nix-docker-build
just nix-coverage    # produces coverage XML

# Multi-platform
nix build .#packages.x86_64-linux.default
nix build .#packages.aarch64-linux.default

# Test suite
just rust-test-e2e
just hurl-test
nix build .#dev && cd build && ctest --output-on-failure  # unit + approval

# Sanity check: migrated deps came from nixpkgs, not local prefix
nix-store -q --references result/bin/sipi | grep -E 'zlib|bzip2|xz|zstd|jansson|expat|lcms2|sqlite'
# Should show nixpkgs store paths

# Approval baseline still passes (ext-image-codec deps NOT migrated yet)
nix build .#dev && cd build && ctest -L approval

# Kakadu derivation builds standalone — exercise all 4 toolchain variants.
# (`nix develop` doesn't change the stdenv that subsequent `nix build` invocations
# use — they always use the flake's overlay default. To exercise gcc14 on darwin,
# expose an explicit override package.)
#
# PR 1 also adds these flake outputs:
#   packages.kakaduGccDarwin = pkgs.kakadu.override { stdenv = pkgs.gcc14Stdenv; };
nix build .#kakadu                                            # darwin: libcxx-clang → Mac-arm-64-clang
nix build .#kakaduGccDarwin                                   # darwin: gcc14 → Mac-arm-64-gcc + 3 patches
nix build .#packages.x86_64-linux.kakadu                      # Linux-x86-64-gcc (via native-linux-builder)
nix build .#packages.aarch64-linux.kakadu                     # Linux-arm-64-gcc (via native-linux-builder)

ls result/lib | grep -E 'libkdu(_aux)?\.a'
ls result/lib/cmake/kakadu/kakaduConfig.cmake

# vendor/ shrunk (8 archives gone)
[ ! -f vendor/zlib-1.3.2.tar.gz ] && echo "vendor cleanup OK"

# ext/ shrunk (8 subdirs gone)
[ ! -d ext/zlib ] && echo "ext cleanup OK"
```

**Acceptance:**
- All build variants green on macOS + Linux x2
- Image-encode approval baseline passes (no drift on un-migrated codec deps)
- E2E + hurl tests pass
- `kakadu` is a standalone Nix derivation; `find_package(kakadu)` works
- `vendor/` and `ext/` reduced by 8 entries

**Rollback:** Single `git revert` restores ext/<dep>/, vendor archives, STATIC IMPORTED blocks, dependencies.cmake entries, and `package.nix` at root.

---

### PR 2 — Groups 2 + 3 (cmake-native + autotools fan-in)

**Motivation:** These 7 deps share a common risk profile — image-codec output bit-exactness (Group 2) and openssl/curl/libmagic version-compat (Group 3). Bundling them lets one approval-suite + integration-smoke run validate both groups with shared setup cost.

**Goal:** Migrate 7 mid-risk deps to nixpkgs.

**Effort:** ~5 days.

**Risk:** Medium. libpng/libtiff/libwebp/exiv2 output bit-exactness is the
test gate. openssl/curl version compatibility is the second.

**Group 2 (cmake-native, 4 deps):** libpng, libtiff, libwebp, exiv2
**Group 3 (autotools fan-in, 3 deps):** openssl, curl, libmagic

**Files deleted (7 ext subdirs + 7 vendor archives):**
- `ext/{png,tiff,webp,exiv2,openssl,curl,libmagic}/`
- `vendor/{libpng-*,tiff-*,libwebp-*,exiv2-*,openssl-*,curl-*,file-*}.tar.{gz,bz2,xz}`

**Files modified:**
- `CMakeLists.txt`: add 7 `find_package`/`pkg_check_modules` calls (per §3.3 final shape: `find_package(PNG REQUIRED)`, `find_package(TIFF REQUIRED)`, `find_package(WebP CONFIG REQUIRED)`, `find_package(exiv2 CONFIG REQUIRED)`, `find_package(OpenSSL REQUIRED)`, `find_package(CURL REQUIRED)`, `pkg_check_modules(libmagic REQUIRED IMPORTED_TARGET GLOBAL libmagic)`); delete 7 `add_subdirectory` lines + STATIC IMPORTED blocks; update `target_link_libraries` (replace `tiff webp jbigkit png` etc. with `TIFF::TIFF PNG::PNG WebP::webp Exiv2::exiv2lib OpenSSL::SSL OpenSSL::Crypto CURL::libcurl PkgConfig::libmagic`)
- `cmake/dependencies.cmake`: delete 7 dep blocks
- `nix/sipi.nix`: add `libpng libtiff libwebp exiv2 openssl curl file` to `buildInputs`
- `scripts/vendor.sh`: shrink dep list

**Verification:**

Standard matrix as PR 1, plus:

```bash
# Image-encode bit-exactness on TIFF/WebP/PNG paths
nix build .#dev && cd build && ctest -L approval --output-on-failure

# OpenSSL + curl integration: sentry init smoke test
# (Note: sentry init at src/sipi.cpp:424-474 is silent — no log on success/failure;
# we just verify the binary doesn't crash on a well-formed DSN at startup, then
# confirm the linkage via ldd/otool.)
SIPI_SENTRY_DSN=https://invalid@sentry.example/1 ./result/bin/sipi --help >/dev/null
ldd result/bin/sipi   2>/dev/null | grep -E 'libssl|libcrypto|libcurl' || \
  otool -L result/bin/sipi | grep -E 'libssl|libcrypto|libcurl'

# CURL integration: contract tests cover shttps's outbound HTTP paths
just hurl-test

# libmagic linkage (no --probe flag; libmagic is used internally by shttps::Parsing)
ldd result/bin/sipi 2>/dev/null | grep -E 'libmagic' || \
  otool -L result/bin/sipi | grep -E 'libmagic'

# Approval test stays the primary correctness gate for libmagic-touching paths
nix build .#dev && cd build && ctest -L approval --output-on-failure
```

**Acceptance:**
- All build variants green on macOS + Linux x2
- Image-encode approval baseline passes for TIFF/PNG/WebP outputs
- Sentry + curl integration smoke checks pass
- libmagic file-type detection unchanged
- `vendor/` and `ext/` reduced by 7 more entries

**Rollback:** Single `git revert`.

---

### PR 3 — Group 4 (version-sensitive deps; libjpeg stays vendored)

**Motivation:** These deps carry version-drift risk (lua 5.3 point release; prometheus/sentry version-compat). Each has a vendor-fallback escape hatch (kakadu/libjpeg pattern) so the PR is bounded — if one dep doesn't migrate cleanly, vendor it; ship the rest.

**Goal:** Migrate 5 version-sensitive deps to nixpkgs; create the vendored
libjpeg derivation.

**Effort:** ~4 days (was ~5; libjpeg-turbo gate eliminated).

**Risk:** Medium. lua and prometheus-cpp/sentry-native version drift are
the main risks; each has a vendor-fallback escape hatch.

**Deps:** jbigkit, lua, luarocks, prometheus-cpp, sentry-native (to nixpkgs); libjpeg (to `nix/libjpeg.nix`).

**libjpeg approach:** vendored IJG 9f as `nix/libjpeg.nix` (defers libjpeg-turbo migration; see §10).

Create `nix/libjpeg.nix` building IJG 9f from a `fetchurl` FOD:

```nix
# nix/libjpeg.nix (sketch)
{ stdenv, lib, fetchurl }:
stdenv.mkDerivation (finalAttrs: {
  pname = "libjpeg";
  version = "9f";
  src = fetchurl {
    url = "https://www.ijg.org/files/jpegsrc.v${finalAttrs.version}.tar.gz";
    sha256 = "...";  # pin at PR-author time
  };
  configureFlags = [
    "--enable-static"
    "--disable-shared"
    "--with-pic"             # required: sipi-the-binary is PIE under nixpkgs default hardening (PR 4)
  ];
  # No patches; standard autotools build.
})
```

Sipi's `CMakeLists.txt` calls `find_package(JPEG REQUIRED)` (same as if
libjpeg-turbo); CMake's standard `FindJPEG.cmake` module locates our
derivation via `buildInputs` and produces the `JPEG::JPEG` imported
target. **No CMake change** vs. the version that would have used nixpkgs
libjpeg-turbo.

**Files added:**
- `nix/libjpeg.nix` (new vendored derivation; replaces `ext/jpeg/`)

**Files deleted (6 ext subdirs + 6 vendor archives):**
- `ext/{jpeg,jbigkit,lua,luarocks,sentry,prometheus-cpp}/`
- corresponding vendor archives (jpeg-9f.tar.gz now sourced via `nix/libjpeg.nix`'s fetchurl FOD instead)

**Files modified:**
- `CMakeLists.txt`: add `find_package(JPEG REQUIRED)`, `find_package(Lua 5.3 EXACT REQUIRED)` + manual `Lua::Lua` imported target (FindLua doesn't create one — see §3.3), `find_package(prometheus-cpp CONFIG REQUIRED)`, `find_package(sentry CONFIG REQUIRED)`; delete 6 `add_subdirectory` + STATIC IMPORTED blocks; update `target_link_libraries` (use `JPEG::JPEG`, `Lua::Lua`, `prometheus-cpp::pull`, `sentry::sentry` per §3.3 final shape)
- `cmake/dependencies.cmake`: delete 6 dep blocks (file is now nearly empty; final cleanup in PR 4)
- `nix/sipi.nix`: add `libjpeg` (from our derivation), `lua5_3 luarocks sentry-native prometheus-cpp` (from nixpkgs) to buildInputs (jbigkit comes via libtiff)
- `flake.nix`: add `libjpeg` derivation to overlay + packages outputs

**Verification:**

```bash
# libjpeg derivation builds standalone (matches kakadu pattern from PR 1)
nix build .#libjpeg
ls result/lib | grep -E 'libjpeg\.a'
ls result/include | grep -E 'jpeglib\.h'

# Full approval suite — the main gate
nix build .#dev && cd build && ctest -L approval --output-on-failure

# Lua paths exercised
just hurl-test  # lua_endpoints.hurl already covers Lua scripting

# Prometheus integration: /metrics endpoint live test
./result/bin/sipi --serverport=1024 --config=config/sipi.localdev-config.lua &
SIPI_PID=$!
sleep 2 && curl -s http://localhost:1024/metrics | head -20
kill $SIPI_PID

# Sentry integration: silent init — verify (a) binary doesn't crash with DSN set,
# (b) sentry symbols are linked.
SIPI_SENTRY_DSN=https://invalid@sentry.example/1 \
  SIPI_SENTRY_ENVIRONMENT=test \
  SIPI_SENTRY_RELEASE=test \
  ./result/bin/sipi --help >/dev/null
ldd result/bin/sipi 2>/dev/null | grep -E 'sentry' || \
  otool -L result/bin/sipi | grep -E 'sentry'

```

**Acceptance:**
- Image-encode approval suite passes (or documented exceptions reviewed and re-approved)
- Lua scripting integration tests pass (`test/hurl/lua_endpoints.hurl`)
- /metrics endpoint produces expected Prometheus output
- Sentry initialization succeeds (no init errors at startup)
- All build variants green on macOS + Linux x2

**Rollback strategy:**
- Single `git revert` if all of Group 4 must back out
- Per-dep partial revert if a single nixpkgs migration fails: cherry-pick the working changes; create `nix/<failed-dep>.nix` for the one that didn't migrate (libjpeg/kakadu pattern)

**Prod-soak window:** none. PR 3 stacks in main with PR 4; weekly prod cuts absorb whatever's merged. Test gate (approval suite + hurl + e2e + integration smoke) is sufficient.

---

### PR 4 — DEV-6282 + Final Cleanup

**Motivation:** Once all deps are nixpkgs-native, sipi's TUs compile in a context that doesn't need `hardeningDisable`. This is the payoff PR — DEV-6282 closes, ext/ and vendor/ disappear, the greenfield shape is reached.

**Goal:** Drop `hardeningDisable` from sipi (resolves DEV-6282). Delete
the now-empty ext/ and vendor/ trees.

**Effort:** ~2 days.

**Risk:** Low-medium. Hardening flags can produce subtle runtime regressions; checksec is the validation gate.

**Hardening (DEV-6282):**

In `nix/sipi.nix`:
```nix
# Before:
hardeningDisable = [ "all" ];

# After: deleted entirely (nixpkgs default applies)
# If a specific flag breaks at link or runtime, narrow in a follow-up
# commit within PR 4 (do not ship a still-disabled hardening as final).
```

`nix/kakadu.nix` and `nix/libjpeg.nix`: keep `hardeningDisable = [ "all" ]`
(proprietary / vendored conservative).
`packages.sanitized` and `packages.fuzz` overrides: keep
`hardeningDisable = [ "all" ]` (sanitizer/fuzzer instrumentation conflict).
Dev shells (`flake.nix:500/534/563`): **also drop** `hardeningDisable`
from the `clang` and `gcc` shells; **keep it** on the `fuzz` shell. The
inner-loop dev shell should match the build-derivation hardening posture
so `cmake --build build` produces binaries with diagnostics matching
`pkgs.sipi`'s.

**Hardening-narrowing micro-procedure.** If dropping `hardeningDisable`
outright produces a build or runtime failure:

1. Identify the failing flag from the build/link error or runtime symptom (commonly: PIE on statically-linked archives; format on legacy printf paths)
2. Add `hardeningDisable = [ "<flag>" ];` (single flag) — not `[ "all" ]`
3. Re-run the full verification matrix (build + checksec + tests on all 3 platforms)
4. Document the disable + reason in a one-line comment above `hardeningDisable`
5. Commit within PR 4 — do not ship a still-`[ "all" ]` disable as the final state

### Closure-bloat assertion (`__structuredAttrs`)

PR 4 adopts `__structuredAttrs = true` in `nix/sipi.nix` and adds
`disallowedReferences` to convert the existing `postFixup`
`remove-references-to` from a "scrub and pray" pattern into a build-time
assertion:

```nix
__structuredAttrs = true;
disallowedReferences = [
  stdenv.cc stdenv.cc.cc stdenv.cc.bintools stdenv.cc.bintools.bintools
  cmake autoconf automake libtool perl  # nativeBuildInputs that must not leak
];
```

Catches the toolchain-leak regression (documented at `package.nix:230-232`
in today's tree) at build time instead of via the manual postFixup. The
postFixup stays as belt-and-braces; if `disallowedReferences` fires, the
build fails loudly with a clear pointer rather than silently bloating
the closure.

**Cleanup:**

**Files deleted:**
- `ext/` (whole tree; only `ext/kakadu/` remained, now in `nix/kakadu.nix`)
- `vendor/` (empty)
- `cmake/dependencies.cmake` (no deps left)
- `scripts/vendor.sh`
- `patches/cmake-openssl.patch` (cosmetic; openssl from nixpkgs builds clean)
- `patches/CMakeLists.poppler.patch` (already dead)

**Files modified:**
- `CMakeLists.txt`: remove vestigial `${COMMON_LOCAL}`, `${COMMON_VENDOR}`, `${COMMON_EXT}` references; remove `add_subdirectory(${COMMON_EXT}/kakadu)` line (kakadu now via find_package); ensure final shape matches §3.3
- `justfile`: delete `vendor-download`, `vendor-verify`, `vendor-checksums`, `kakadu-fetch` recipes (kakadu still fetched via FOD on Nix-build path)
- `docs/src/development/building.md`, `docs/src/development/kakadu.md`, `docs/src/development/developing.md`: update to reflect new layout (no vendor step; nix-native deps)
- `nix/sipi.nix`: drop hardeningDisable; ensure CMakeLists doesn't re-invoke disabled hardening flags via cmakeFlags

**Verification:**

```bash
# Hardening landed
nix build .#release
checksec --file=result/bin/sipi
# Expected:
#   RELRO            : Full RELRO
#   STACK CANARY     : Canary found
#   NX               : NX enabled
#   PIE              : PIE enabled  (or "No PIE" if static-linking precludes)
#   FORTIFY          : Yes
#   RPATH            : (whatever nixpkgs default is)

# Sanitized variant unchanged
nix build .#sanitized
checksec --file=result/bin/sipi
# Expected: NX only; RELRO partial or none; canary No; FORTIFY No

# Fuzz variant unchanged
nix build .#fuzz
checksec --file=result/bin/sipi
# Expected: similar to sanitized

# Production Docker still builds
nix build .#docker-stream

# Tree cleanup
[ ! -d ext ] && echo "ext/ deleted"
[ ! -d vendor ] && echo "vendor/ deleted"
[ ! -f cmake/dependencies.cmake ] && echo "dependencies.cmake deleted"
[ ! -f scripts/vendor.sh ] && echo "vendor.sh deleted"

# Final test pass
just nix-build && just nix-build-default && just nix-build-release
just nix-build-sanitized && just nix-build-fuzz && just nix-docker-build
just rust-test-e2e && just hurl-test
nix build .#dev && cd build && ctest --output-on-failure  # all unit + approval

# Multi-platform
nix build .#packages.x86_64-linux.default
nix build .#packages.aarch64-linux.default
```

**Acceptance:**
- `checksec result/bin/sipi` (`.#release`) shows full hardening (PIE optional)
- `checksec` on `.#sanitized` and `.#fuzz` unchanged
- `ext/`, `vendor/`, `cmake/dependencies.cmake`, `scripts/vendor.sh` deleted
- `just --list` no longer shows vendor-* or kakadu-fetch recipes
- All build variants green on macOS + Linux x2
- Image-encode approval baseline still passes

**Rollback:** Single `git revert`. Note that the cleanup PR has a large
deletion footprint; review the revert carefully if hardening breaks but
cleanup is fine — these can be split via `git revert -n` then partial commit.

---

## 8. Key Technical Decisions

### 8.1 libjpeg — vendor IJG 9f

libjpeg stays at IJG 9f, packaged as `nix/libjpeg.nix` (small Nix
derivation pulling sources via fetchurl from `ijg.org`, sha256-pinned).
libjpeg-turbo migration is deferred to a future plan (§10).

Maintenance cost: ~0.5h per nixpkgs/stdenv major bump (no patches;
straightforward autotools build).

### 8.2 lua 5.3.5 → nixpkgs lua5_3

nixpkgs `lua5_3` ships the latest 5.3.x point release (5.3.6+). Sipi's
bindings are 5.3-API-locked (not 5.4). Point-release upgrade should be
ABI-clean. Verify via `test/hurl/lua_endpoints.hurl` and the e2e tests.

### 8.3 Output naming convention

Today's `STATIC IMPORTED` targets reference `local/lib/lib{curl_static,magic_static,openssl_ssl,openssl_crypto,kdu_aux}.a` — sipi-internal naming, not nixpkgs convention. After PR 4, all imported targets are nixpkgs-style (`OpenSSL::SSL`, `CURL::libcurl`, `kakadu::kdu_aux`). The `_static` suffix disappears (deps come dynamic from nixpkgs and link in via the standard cc-wrapper machinery; static linking of the final sipi binary is a post-plan concern, not relevant here).

### 8.4 kakadu derivation toolchain matrix

The kakadu Makefiles branch on `KDU_EXEC_PLATFORM`:
- `Mac-arm-64-clang` — sipi's `pkgs.sipi` on macOS (libcxx clang stdenv)
- `Mac-arm-64-gcc` — sipi's `gcc` dev shell on macOS (gcc14 stdenv); needs the 3 patches
- `Linux-x86-64-gcc` — sipi's `pkgs.sipi` on linux-x86_64
- `Linux-arm-64-gcc` — sipi's `pkgs.sipi` on linux-aarch64

`nix/kakadu.nix` derives `KDU_EXEC_PLATFORM` from `stdenv.hostPlatform` +
`stdenv.cc.libcxx` (or compiler-family) at evaluation time. Patches
applied conditionally on `Mac-arm-64-gcc` only.

### 8.5 `cmake/dependencies.cmake` lifecycle

- PR 1: shrinks (8 entries removed)
- PR 2: shrinks further (7 entries removed)
- PR 3: shrinks to near-empty (6 entries removed)
- PR 4: deleted entirely

No source-of-truth split or codegen needed — the file shrinks naturally
toward zero across PRs.

### 8.6 Patches lifecycle

- PR 1: no patch changes
- PR 2: openssl swap: `cmake-openssl.patch` no longer applied (still in tree)
- PR 4: delete `cmake-openssl.patch`, `CMakeLists.poppler.patch`. Surviving: 3 kakadu patches, applied by `nix/kakadu.nix`.

## 9. Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| nixpkgs lua5_3 ABI drift vs in-tree 5.3.5 | Low | Medium | Hurl + e2e tests exercise Lua paths. If broken, vendor lua5_3.5 via `nix/lua5_3.nix`. |
| nixpkgs prometheus-cpp / sentry-native version mismatch with sipi's API expectations | Medium | Medium | Smoke-test `/metrics` + Sentry init at PR 3 verification. If broken, vendor or pin nixpkgs to a specific commit. |
| kakadu Makefile platform selection logic breaks on a specific stdenv | Medium | High | Test all 4 toolchain variants (libcxx-clang darwin / gcc14 darwin / Linux-x86-64-gcc / Linux-arm-64-gcc) at PR 1 verification (see PR 1 Verification block). The 3 patches only apply on `Mac-arm-64-gcc`. |
| `find_package` + modern imported targets miss a transitive dep that the current hand-ordered link line carried implicitly | Medium | Low-medium | CMake errors at link time (loud failure). Add the missing `target_link_libraries(sipi PRIVATE …)` entry. |
| PIE on sipi-the-binary fails to link kakadu/libjpeg static archives (need PIC objects) | Medium | Medium | sipi-the-binary becomes PIE under nixpkgs default hardening; static archives (`libkdu*.a`, `libjpeg.a`) must be `-fPIC`. Mitigation: `nix/libjpeg.nix` configureFlags include `--with-pic`; `nix/kakadu.nix` Makefiles need verification — if the kakadu build doesn't produce PIC objects, either patch the Makefile, build kakadu with `-fPIC` via `CFLAGS`/`CXXFLAGS` env, or fall back to `hardeningDisable = [ "pie" ]` on `nix/sipi.nix` (per §7 PR 4 narrowing micro-procedure). |
| WebP / Exiv2 / zstd CMake config target naming differs across nixpkgs versions | Medium | Medium | Plan §3.3 pins exact target names (`WebP::webp`, `Exiv2::exiv2lib`, `zstd::libzstd_shared`). PR 1/2 verification catches mismatches at link time. Fall back to `pkg_check_modules` if a Config.cmake disappears in nixpkgs. |
| nixpkgs libwebp pre-2024-10 lacks Config.cmake | Low | Medium | Verify nixpkgs pin is post-Oct-2024 (PR #335702 in nixpkgs). `flake.lock` should already have a recent enough pin. |
| Cachix push exposes built kakadu artifacts beyond DaSCH | Low | Medium | DaSCH's existing Cachix is private/authenticated. Don't switch kakadu to a public cache. |
| `__structuredAttrs` interaction with `separateDebugInfo` regression | Low | Medium | nixpkgs supports both together since ~2023. PR 4 verification includes building `.#default.debug` to confirm. |
| macOS-only failure missed in CI (Linux-only) | High | Medium | CLAUDE.md invariant: every PR builds locally on darwin-aarch64 + native-linux-builder before push. |
| nixpkgs version drift mid-migration (e.g., openssl bump between PR 2 and PR 3) | Low | Low | `flake.lock` frozen for the migration (§5.1). |
| Stale-checkout teammate breakage (vendor archives gone post-merge) | Medium | Low | Rollout comms (§5.3) before each large-delete PR; pin in #sipi-dev for 48h. |

## 10. Out of Scope

These are real follow-up initiatives, captured here to make the boundary
of *this* plan explicit:

1. **pkgsStatic-based static binaries.** Replace the Zig-based static
   path (deleted in PR 0) with `pkgsStatic.callPackage ./nix/sipi.nix`
   and `pkgsCross.aarch64-multiplatform-musl.pkgsStatic.sipi`. Trivial
   *after* this plan because `nix/sipi.nix` is already nixpkgs-native.
   Triggered when a Docker-image-static-swap initiative drives it.
2. **libjpeg-turbo migration.** Replace the vendored IJG 9f
   (`nix/libjpeg.nix`) with nixpkgs `libjpeg` (libjpeg-turbo). Carries
   real ABI risk (output bit-exactness in low-quality / unusual color
   subsampling paths). Trigger: SIMD-perf demand, IJG going
   unmaintained, or a robust-enough approval suite to evaluate the
   swap painlessly.
3. **shttps separation.** Per `sipi/CONTEXT-MAP.md`, shttps is a
   strangler-fig target. Move shttps to its own library + CMakeLists +
   Nix derivation. Independent of this plan.
4. **Opportunistic ABI-pinned vendored exceptions.** If PR 3 reveals that
   prometheus-cpp / sentry-native / lua diverge unacceptably from their
   nixpkgs versions, the per-dep `nix/<dep>.nix` derivation lives forever
   (libjpeg/kakadu pattern). Not a follow-up — it's the PR 3 fallback
   path.
5. **`flake.lock` update cadence.** Establishing a dependabot-style
   automation for keeping nixpkgs pin fresh. Tracked separately.

## 11. End-to-End Verification (overall acceptance)

All of the following must hold after PR 4 merges:

```bash
# Greenfield shape
[ ! -d ext ]                              # ext/ deleted
[ ! -d vendor ]                           # vendor/ deleted
[ ! -f cmake/dependencies.cmake ]         # dependencies.cmake deleted
[ ! -f cmake/zig-toolchain.cmake ]        # zig deleted (PR 0)
[ ! -f scripts/vendor.sh ]                # vendor.sh deleted
[ ! -f patches/cmake-openssl.patch ]      # patch deleted
[ ! -f patches/CMakeLists.poppler.patch ] # dead patch deleted
ls patches/ | wc -l                       # 3 (kakadu-makefile-{apps,coresys,managed}.patch)

# CMakeLists slimmed
wc -l CMakeLists.txt                      # ~80–100 lines (was ~600+)

# Nix layout
ls nix/                                   # sipi.nix, kakadu.nix, kakadu-archive.nix, libjpeg.nix

# Build matrix on every supported platform
just nix-build            && just nix-build-default
just nix-build-release    && just nix-build-sanitized
just nix-build-fuzz       && just nix-docker-build
just nix-coverage
nix build .#packages.x86_64-linux.default
nix build .#packages.aarch64-linux.default

# Tests
just rust-test-e2e
just hurl-test
nix build .#dev && cd build && ctest --output-on-failure

# Hardening (DEV-6282)
nix build .#release && checksec --file=result/bin/sipi
# Expected: STACK CANARY=Yes, NX=enabled, RELRO=Full, FORTIFY=Yes, PIE=Yes-or-No

nix build .#sanitized && checksec --file=result/bin/sipi
# Expected: NX only; canary No; RELRO partial; FORTIFY No (sanitizer-compatible)

# Image-encode bit-exactness
nix build .#dev && cd build && ctest -L approval --output-on-failure

# Cache granularity sanity check
nix build .#default
echo "// trivial change" >> src/sipi.cpp
nix build .#default                       # only sipi-the-target rebuilds
```

DEV-6282 is closed when checksec confirms hardening on `.#release`.

## 12. Critical Files Reference

| Path | Role |
|---|---|
| `sipi/CMakeLists.txt` | Top-level build; reduced from ~600 lines to ~80–100 across PRs |
| `sipi/flake.nix` | Variants + dockerTools + dev shells; `mkStaticBuild` removed in PR 0 |
| `sipi/nix/sipi.nix` | Sipi derivation (new in PR 1; replaces `package.nix` at root) |
| `sipi/nix/kakadu.nix` | Kakadu derivation (new in PR 1) |
| `sipi/nix/kakadu-archive.nix` | FOD archive (lifted from flake.nix in PR 1) |
| `sipi/nix/libjpeg.nix` | Vendored IJG 9f derivation (new in PR 3; defers libjpeg-turbo) |
| `sipi/test/approval/image_encode_baseline_test.cpp` | New approval suite (test investment §6.1, lands in PR 0) |
| `sipi/test/_test_data/images/encode_baseline/` | Baseline input images for the approval suite (PR 0) |
| `sipi/test/approval/CHANGELOG.approval.md` | Re-approval audit log (created empty in PR 0) |
| `sipi/test/hurl/{health,metrics,iiif_transform,info_json}.hurl` | New hurl coverage (§6.2, lands in PR 0) |
| `sipi/cmake/dependencies.cmake` | Shrinks across PRs 1–3; deleted in PR 4 |
| `sipi/scripts/vendor.sh` | Shrinks across PRs 1–3; deleted in PR 4 |
| `sipi/justfile` | `vendor-*` recipes deleted in PR 4 |
| `sipi/.github/workflows/publish.yml` | Static publication steps deleted in PR 0 |
| `sipi/.github/workflows/test.yml` | No structural changes; verification target throughout |
| `sipi/.github/workflows/sanitizer.yml` | No structural changes; sanitized must remain unhardened |
| `sipi/CLAUDE.md` | Multi-platform + reproducibility invariants; reference per PR |

## 13. Execution Sequence Summary

```
PR 0          Drop Zig static path + test investment       ~2 days   (Risk: none)
              - Zig deletion (~1 day)
              - Image-encode approval baseline (§6.1)
              - Hurl gap fills (§6.2)
              ↓ ship; goldens captured against unmigrated build

PR 1          Foundation + Group 1 (8 zero-fan-in deps)     ~5 days   (Risk: low)
              ↓ ship; can stack in main with PR 2

PR 2          Groups 2 + 3 (cmake-native + autotools fan-in) ~5 days   (Risk: medium)
              ↓ ship; can stack in main with PR 3

PR 3          Group 4 (lua/prom/sentry/luarocks/jbigkit + nix/libjpeg.nix)
                                                            ~4 days   (Risk: medium)
              ↓ ship; can stack in main with PR 4

PR 4          DEV-6282 + Cleanup                            ~2 days   (Risk: low-medium)
              ↓ ship

Total active work:        ~18 days  (PR 0 incl. investment + 1 + 2 + 3 + 4)
Calendar (PRs stack):     ~3-4 weeks of merge work
Prod observation overlap: ~5 weeks total (one weekly cut per merged PR)
Recommended rhythm:       merge as fast as review/CI passes; weekly
                          prod cuts absorb whatever's merged that week.
                          If anything surfaces in prod, address via
                          forward-fix PR (cross-PR rollback per §5.2)
                          rather than stacking reverts.
```

DEV-6282 is closed by PR 4. Greenfield shape is reached at PR 4 merge.
pkgsStatic / shttps separation are independent follow-up plans triggered
by their own drivers, not this plan.
