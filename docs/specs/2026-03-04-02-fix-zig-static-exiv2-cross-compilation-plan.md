---
title: "Fix zig-static exiv2 cross-compilation (features.h/bits/wordsize.h)"
date: 2026-03-04
type: fix
status: implemented
repositories:
  - sipi
---

## Enhancement Summary

**Deepened on:** 2026-03-04
**Reviewed on:** 2026-03-04
**Research agents used:** cmake-compiler-detection, zig-toolchain-examples, zig-env-sysroot, exiv2-cmake-internals
**Review agents used:** code-simplicity-reviewer, cpp-reviewer, specification-reviewer
**Language skills applied:** C++ (CMake)

### Key Improvements
1. Confirmed exiv2 does NOT override CMAKE_CXX_FLAGS — root cause is cmake's compiler identification probe adding host paths
2. Discovered `CMAKE_C_COMPILER_TARGET`/`CMAKE_CXX_COMPILER_TARGET` as a cleaner cmake-native approach
3. Validated Approach B fallback: `zig env` JSON output provides `lib_dir` for sysroot construction
4. Added `CMAKE_<LANG>_COMPILER_TARGET` as mandatory (not optional) based on review feedback

### Review Findings Applied
- Merged Approach A and A-bis into a single approach (eliminates unnecessary CI round-trips)
- Included `CMAKE_FIND_ROOT_PATH_MODE_*` in primary approach (standard cross-compilation boilerplate)
- Corrected Approach B: `CMAKE_SYSROOT` DOES pass `--sysroot=` to the compiler (zig bugs apply) — use `CMAKE_FIND_ROOT_PATH` instead
- Added cleanup of double `-target` injection (wrapper + `CMAKE_C_FLAGS_INIT`)
- Added terminal failure strategy, rollback procedure, and expanded acceptance criteria
- Collapsed 6 implementation steps to 3

### New Considerations Discovered
- Even without forwarding toolchain file, `-DCMAKE_SYSTEM_NAME=Linux` triggers compiler probing that may inject `/usr/include`
- Zig's `--sysroot` flag has known bugs (issues #24368, #19638) — avoid it; `CMAKE_SYSROOT` passes `--sysroot=` to compiler
- `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` skips linker checks but NOT include path probing
- Wrapper scripts + `CMAKE_<LANG>_COMPILER_TARGET` may both inject `--target`/`-target` — zig tolerates duplicates

---

# Fix: zig-static exiv2 cross-compilation failure

> **Type**: fix
> **Date**: 2026-03-04
> **PR**: [dasch-swiss/sipi#510](https://github.com/dasch-swiss/sipi/pull/510)
> **Blocking**: Both `zig-static / amd64` and `zig-static / arm64` CI jobs

## Problem

When cross-compiling for musl targets (`x86_64-linux-musl`, `aarch64-linux-musl`), exiv2's C++ source files fail with:

```
/usr/include/features-time64.h:20:10: fatal error: 'bits/wordsize.h' file not found
```

The include chain is:
```
exiv2/slice.hpp → <cassert>
  → zig's libc++/include/__config
  → __configuration/abi.h
  → __configuration/platform.h:36
  → /usr/include/features.h        ← HOST glibc, NOT musl!
  → /usr/include/features-time64.h
  → bits/wordsize.h (NOT FOUND — glibc multilib header, not in Ubuntu runner)
```

**Key observation**: All pure C deps (zlib, png, jpeg, tiff, etc.) and even exiv2's own xmpsdk (C-like code) compile fine. Only exiv2's main C++ code fails — specifically when libc++ headers include `<features.h>` to detect the platform.

## Root Cause Analysis

The `-target x86_64-linux-musl` flag IS present in cmake's `CMAKE_CXX_FLAGS` (confirmed in CI logs). The zig wrapper scripts also inject `-target` from `.zig-target`. Yet zig's libc++ still resolves `<features.h>` to `/usr/include/features.h` (host glibc) instead of zig's bundled musl headers.

**Why pure C works but C++ doesn't**: When zig compiles C code with `-target musl`, it correctly uses musl's own `features.h` from its bundled sysroot. But for C++, zig uses its bundled libc++ headers, and libc++'s `__configuration/platform.h` does `#include <features.h>` which falls through to the host `/usr/include` path before zig's musl sysroot is searched.

**What we've tried (and failed)**:
1. `CMAKE_CXX_FLAGS_INIT` with `-target` in toolchain file — flag doesn't propagate to exiv2's compile commands
2. `-DCMAKE_CXX_FLAGS=-target ... -O2` passed directly — flag IS in cmake's reported flags but compiler still finds host headers
3. Forwarding `CMAKE_TOOLCHAIN_FILE` to ExternalProject — toolchain loads correctly (confirmed by log messages) but doesn't fix the include path issue

**The missing piece**: The zig toolchain file sets `CMAKE_SYSTEM_NAME=Linux` and `CMAKE_CROSSCOMPILING=TRUE` but does NOT set:
- `CMAKE_<LANG>_COMPILER_TARGET` — tells cmake's compiler probe to pass `--target=` during identification
- `CMAKE_FIND_ROOT_PATH` — tells cmake where to search for target headers/libs
- `CMAKE_FIND_ROOT_PATH_MODE_INCLUDE` — controls whether host `/usr/include` is searched

Without these, cmake's compiler detection discovers `/usr/include` as an implicit system include directory. When exiv2's cmake runs its own `project()` command, it re-probes the compiler, and the probing process injects host include paths that override zig's sysroot-based resolution.

### Research Insights: Exiv2 Internals

**Confirmed**: Exiv2 v0.28.5's `cmake/compilerFlags.cmake` uses **APPEND** operations (`set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ...")`) and `add_compile_options()` — it does NOT override or replace CMAKE_CXX_FLAGS. The `-target` flag, if present, is preserved through the build.

**Execution order**: `project()` → `mainSetup.cmake` → `findDependencies.cmake` → `compilerFlags.cmake` → `add_subdirectory(xmpsdk)` → `add_subdirectory(src)`. The problem occurs at `project()` time during compiler identification, before any flags are set.

### Research Insights: CMake Compiler Identification

When cmake's `project()` command runs, it probes the compiler with `-v` flag (`echo | zig cc -E -v -`) to discover `CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES`. If the probe output includes `/usr/include` as an implicit path, cmake adds it to ALL compilation commands via the implicit include mechanism. This happens transparently — it's not visible in `CMAKE_CXX_FLAGS`.

**Critical finding**: `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` skips linking but does NOT skip include path probing. The implicit include detection still runs.

**`CMAKE_<LANG>_COMPILER_TARGET`**: For clang-family compilers (which zig cc reports as), cmake uses this variable to pass `--target=` during its own compiler identification probe. This directs zig to use the correct sysroot during probing, preventing `/usr/include` from appearing in the implicit include list.

## Approach: Compiler-only forwarding with full cross-compilation settings

**Rationale**: The wrapper scripts already inject `-target` for every invocation. Forwarding the toolchain file causes cmake to re-run its full compiler detection in the ExternalProject, which is where host paths leak in. Instead, pass explicit compiler paths, `CMAKE_<LANG>_COMPILER_TARGET` for correct probe behavior, and `CMAKE_FIND_ROOT_PATH_MODE_*` to block host path searching.

**Risk**: LOW. All settings are standard cmake cross-compilation boilerplate.

**Note on duplicate `-target` flags**: The wrapper scripts inject `-target x86_64-linux-musl` and cmake's `COMPILER_TARGET` mechanism may also inject `--target=x86_64-linux-musl`. Zig (like clang) tolerates duplicate target flags. Verify in VERBOSE output.

### Fallback: `CMAKE_FIND_ROOT_PATH` with zig's bundled sysroot

If the primary approach fails, parse `zig env` to get `lib_dir` and set `CMAKE_FIND_ROOT_PATH` (NOT `CMAKE_SYSROOT` — see correction below) to point cmake's find operations at zig's bundled headers.

**Correction from review**: The original plan stated `CMAKE_SYSROOT` "only affects cmake's find operations." This is **incorrect** — `CMAKE_SYSROOT` passes `--sysroot=` to the compiler. Given zig's known `--sysroot` bugs ([#24368](https://github.com/ziglang/zig/issues/24368), [#19638](https://github.com/ziglang/zig/issues/19638)), use `CMAKE_FIND_ROOT_PATH` instead, which truly only affects cmake's find operations without passing flags to the compiler.

### Terminal Failure Strategy

If all approaches fail: switch to a container-based cross-compilation environment (Alpine-based Docker with musl-native toolchain) or vendor a pre-compiled exiv2 static library for the target architectures. This avoids the host/target header conflict entirely.

## Implementation Plan

### Step 1: Apply full fix to `cmake/zig-toolchain.cmake`

All changes in a single commit. Three sub-parts:

**1a. Add `COMPILER_TARGET` to main toolchain** (in the cross-compilation block, after the `CMAKE_SYSTEM_PROCESSOR` block, before `set(ENV{ZIG_TARGET})`):
```cmake
# Tell cmake the target triple for compiler identification probes.
# For clang-family compilers (which zig reports as), cmake passes
# --target= during its own probing, directing zig to use the correct
# sysroot for implicit include detection.
set(CMAKE_C_COMPILER_TARGET "${ZIG_TARGET}")
set(CMAKE_CXX_COMPILER_TARGET "${ZIG_TARGET}")
```

**1b. Add `FIND_ROOT_PATH_MODE` to main toolchain** (in the cross-compilation block):
```cmake
# Prevent cmake from searching host paths during cross-compilation.
# Without these, find_path/find_library/find_package may locate
# host headers/libraries instead of our cross-compiled ones.
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
```

**1c. Remove `-target` from `CMAKE_C_FLAGS_INIT`/`CMAKE_CXX_FLAGS_INIT`** (cleanup double injection):

The current toolchain sets `-target ${ZIG_TARGET}` in `FLAGS_INIT`, but the wrapper scripts already inject `-target`. Remove the redundant flag from `FLAGS_INIT` to avoid confusion. Keep only non-target flags:
```cmake
set(CMAKE_C_FLAGS_INIT "")
# -Wno-error=date-time: zig cc treats __DATE__/__TIME__ as errors for
# reproducibility; some deps (exiv2) use them.
set(CMAKE_CXX_FLAGS_INIT "-Wno-error=date-time")
```

**1d. Replace `ZIG_EP_CMAKE_ARGS`**:
```cmake
# --- Helper variables for ExternalProject CMake deps ---
# Cross-compilation: forward compiler paths and cross-compilation settings
# so ExternalProject deps use the same zig compiler wrappers.
# The wrapper scripts (zig-cc, zig-c++) handle -target injection automatically
# via the .zig-target marker file — no need to forward the full toolchain file.
# Forwarding CMAKE_TOOLCHAIN_FILE causes cmake to re-probe the compiler in the
# child project's project() call, which can inject host include paths
# (/usr/include) that break musl builds.
# Native builds: don't forward — the system default compiler is compatible.
set(ZIG_EP_CMAKE_ARGS "" CACHE STRING "CMake args for ExternalProject cmake deps" FORCE)
if(DEFINED ZIG_TARGET AND NOT ZIG_TARGET STREQUAL "")
    set(ZIG_EP_CMAKE_ARGS
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_COMPILER_TARGET=${ZIG_TARGET}
        -DCMAKE_CXX_COMPILER_TARGET=${ZIG_TARGET}
        -DCMAKE_AR=${CMAKE_AR}
        -DCMAKE_RANLIB=${CMAKE_RANLIB}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}
        -DCMAKE_CROSSCOMPILING=TRUE
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
        "-DCMAKE_C_FLAGS=-O2"
        "-DCMAKE_CXX_FLAGS=-O2 -Wno-error=date-time"
        CACHE STRING "" FORCE)
endif()
```

Also add `VERBOSE=1` to exiv2's `BUILD_COMMAND` in `ext/exiv2/CMakeLists.txt` for debugging (remove after CI passes).

### Step 2: Push and verify CI

Commit all changes and push. Verify:
- [ ] `zig-static / amd64` passes
- [ ] `zig-static / arm64` passes
- [ ] `zig-macos / arm64` still passes (no regression — `ZIG_EP_CMAKE_ARGS` empty for native builds)
- [ ] `gcc / ubuntu-24.04` and `gcc / ubuntu-24.04-arm` still pass
- [ ] VERBOSE output shows no `/usr/include` paths in exiv2 compile commands
- [ ] All C++ ExternalProject deps compile: exiv2, fmt, sentry, shttp, gtest, jansson

### Step 3: If Step 2 fails — Fallback to `CMAKE_FIND_ROOT_PATH` with zig sysroot

Parse `zig env` to get `lib_dir`, set `CMAKE_FIND_ROOT_PATH` (not `CMAKE_SYSROOT`):
```cmake
execute_process(
    COMMAND ${ZIG_EXECUTABLE} env
    OUTPUT_VARIABLE _zig_env_json
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(JSON _zig_lib_dir GET "${_zig_env_json}" "lib_dir")
# Point cmake's find operations at zig's bundled headers.
# Using CMAKE_FIND_ROOT_PATH (not CMAKE_SYSROOT) because CMAKE_SYSROOT
# passes --sysroot= to the compiler, which triggers known zig bugs
# (#24368, #19638).
set(CMAKE_FIND_ROOT_PATH "${_zig_lib_dir}/libc")
```

If this also fails: switch to container-based cross-compilation (see Terminal Failure Strategy above).

After CI passes: remove VERBOSE from exiv2, final push.

## Rollback Procedure

If this change causes regressions in gcc or macOS jobs:
1. Revert the `ZIG_EP_CMAKE_ARGS` change — restore the original `CMAKE_TOOLCHAIN_FILE` forwarding
2. Revert `CMAKE_C_FLAGS_INIT`/`CMAKE_CXX_FLAGS_INIT` to include `-target`
3. The exiv2 cross-compilation failure will return, but other deps and non-zig builds will be unaffected
4. Git: `git revert <commit-sha>` for a clean revert

## Acceptance Criteria

- [ ] `zig-static / amd64` CI job passes (all deps compile, unit tests pass, e2e tests pass, static linkage verified)
- [ ] `zig-static / arm64` CI job passes (all deps compile, unit tests pass, e2e tests pass, static linkage verified)
- [ ] `zig-macos / arm64` CI job continues to pass (no regression)
- [ ] `gcc / ubuntu-24.04` and `gcc / ubuntu-24.04-arm` continue to pass
- [ ] No host `/usr/include` paths appear in exiv2's compile commands (verify via VERBOSE output)
- [ ] All C++ ExternalProject deps compile successfully: exiv2, fmt, sentry, shttp, gtest, jansson
- [ ] VERBOSE build flag removed from exiv2 after verification

## Sentry Internal Deps Note

Sentry-native has nested submodules (breakpad/crashpad) built via `add_subdirectory()`, not `ExternalProject_Add`. These inherit cmake variables from the parent scope, so `ZIG_EP_CMAKE_ARGS` forwarded to sentry's top-level cmake should propagate correctly. Verify in CI output that sentry compiles without errors.

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Primary approach doesn't fix include path issue | HIGH — still blocked | Fallback: `CMAKE_FIND_ROOT_PATH` with zig sysroot; terminal: container-based build |
| Removing toolchain forwarding breaks ABI for C++ deps | MEDIUM — libc++ vs libstdc++ | Wrappers use zig which always uses libc++; verify by checking for `std::__1::` symbols |
| Exiv2 cmake overrides compiler settings | LOW | Confirmed: exiv2 appends, doesn't override. Explicit `-DCMAKE_C_COMPILER` on CLI takes precedence |
| Other C++ deps (fmt, sentry) also fail | MEDIUM | Same fix applies; all use `${ZIG_EP_CMAKE_ARGS}` |
| Duplicate `--target`/`-target` from wrapper + COMPILER_TARGET | LOW | Clang tolerates duplicates; verify in VERBOSE output |
| `zig env` JSON format changes between versions | LOW | CMake 3.28 has `string(JSON)` parser; structure is stable since zig 0.12+ |
| Stale cmake cache from failed ExternalProject builds | LOW | CI always builds from clean checkout; local devs: clean build dir |

## References

- [CMake Cross Compiling](https://cmake.org/cmake/help/book/mastering-cmake/chapter/Cross%20Compiling%20With%20CMake.html)
- [CMAKE_<LANG>_COMPILER_TARGET](https://cmake.org/cmake/help/latest/variable/CMAKE_LANG_COMPILER_TARGET.html)
- [CMAKE_FIND_ROOT_PATH](https://cmake.org/cmake/help/latest/variable/CMAKE_FIND_ROOT_PATH.html)
- [zig cc as GCC/Clang replacement](https://andrewkelley.me/post/zig-cc-powerful-drop-in-replacement-gcc-clang.html)
- [Cross-compile with Zig](https://zig.news/kristoff/cross-compile-a-c-c-project-with-zig-3599)
- [Zig --sysroot bug #24368](https://github.com/ziglang/zig/issues/24368)
- [Zig --sysroot include bug #19638](https://github.com/ziglang/zig/issues/19638)
- [mrexodia/zig-cross](https://github.com/mrexodia/zig-cross) — reference zig cmake toolchain
- [tayne3/zig.toolchain.cmake](https://github.com/tayne3/zig.toolchain.cmake) — lightweight zig toolchain
