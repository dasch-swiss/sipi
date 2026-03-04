# cmake/zig-toolchain.cmake
#
# CMake toolchain file for building SIPI with Zig's C/C++ compiler.
# Supports both native builds (local development) and cross-compilation.
#
# Usage — native build (local dev on macOS or Linux):
#   cmake -S . -B build \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/zig-toolchain.cmake
#
# Usage — cross-compile for Linux:
#   cmake -S . -B build \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/zig-toolchain.cmake \
#     -DZIG_TARGET=x86_64-linux-musl
#
# Supported ZIG_TARGET values:
#   <not set>             — native build for the host (macOS or Linux)
#   x86_64-linux-musl     — fully static Linux amd64 binary
#   aarch64-linux-musl    — fully static Linux arm64 binary
#   x86_64-linux-gnu      — dynamically linked Linux amd64 (glibc)
#   aarch64-linux-gnu     — dynamically linked Linux arm64 (glibc)
#
# Prerequisites:
#   - Zig >= 0.15.x installed and on PATH (https://ziglang.org/download/)
#   - Build tools (cmake, autoconf, automake, libtool) for external dependencies

cmake_minimum_required(VERSION 3.28)

# --- Find Zig ---
find_program(ZIG_EXECUTABLE zig REQUIRED)
message(STATUS "Found Zig: ${ZIG_EXECUTABLE}")

# Resolve version
execute_process(
    COMMAND ${ZIG_EXECUTABLE} version
    OUTPUT_VARIABLE ZIG_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "Zig version: ${ZIG_VERSION}")

# Resolve zig's lib directory for musl include paths.
# Zig's lib dir is always at <zig_install>/lib relative to the executable.
get_filename_component(_zig_bin_dir "${ZIG_EXECUTABLE}" DIRECTORY)
set(_zig_lib_dir "${_zig_bin_dir}/lib")
message(STATUS "Zig lib dir: ${_zig_lib_dir}")

# --- Locate wrapper scripts ---
# Wrapper scripts solve the "two-word command" problem with CMake's CMAKE_C_COMPILER.
get_filename_component(_toolchain_dir "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(_wrapper_dir "${_toolchain_dir}/scripts/toolchains")

if(NOT EXISTS "${_wrapper_dir}/zig-cc")
    message(FATAL_ERROR "Zig CC wrapper not found at ${_wrapper_dir}/zig-cc. "
                        "Create it or adjust the path.")
endif()
if(NOT EXISTS "${_wrapper_dir}/zig-c++")
    message(FATAL_ERROR "Zig C++ wrapper not found at ${_wrapper_dir}/zig-c++. "
                        "Create it or adjust the path.")
endif()

# --- Set compilers to Zig wrappers ---
set(CMAKE_C_COMPILER "${_wrapper_dir}/zig-cc")
set(CMAKE_CXX_COMPILER "${_wrapper_dir}/zig-c++")
set(CMAKE_AR "${_wrapper_dir}/zig-ar" CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB "true" CACHE FILEPATH "Ranlib (no-op for Zig)")

# --- Determine build mode: native vs cross-compile ---
set(ZIG_STATIC_BUILD OFF)
set(_zig_native ON)
set(_is_cross_compiling FALSE)

if(DEFINED ZIG_TARGET AND NOT ZIG_TARGET STREQUAL "")
    # =============================================
    # Targeted build mode (ZIG_TARGET is set)
    # =============================================
    set(_zig_native OFF)

    # Parse target triple
    string(REPLACE "-" ";" _target_parts "${ZIG_TARGET}")
    list(GET _target_parts 0 _arch)
    list(GET _target_parts 1 _os)
    list(GET _target_parts 2 _abi)

    # Native-targeted mode vs true cross-compilation.
    # The built binary can only run on the build host when both arch and OS match.
    # Normalize arch: macOS reports "arm64" but Zig triples use "aarch64".
    set(_host_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    if(_host_arch STREQUAL "arm64")
        set(_host_arch "aarch64")
    endif()
    # Normalize OS: CMake uses "Linux"/"Darwin", Zig triples use "linux"/"macos".
    string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" _host_os)
    if(_host_os STREQUAL "darwin")
        set(_host_os "macos")
    endif()

    if(_host_arch STREQUAL "${_arch}" AND _host_os STREQUAL "${_os}")
        set(_is_cross_compiling FALSE)
        message(STATUS "Zig native-targeted mode: ${ZIG_TARGET}")
    else()
        set(_is_cross_compiling TRUE)
        message(STATUS "Zig cross-compilation mode: ${ZIG_TARGET}")
    endif()

    # Map OS to CMake system name
    if(_os STREQUAL "linux")
        set(CMAKE_SYSTEM_NAME Linux)
    elseif(_os STREQUAL "macos")
        set(CMAKE_SYSTEM_NAME Darwin)
    else()
        message(FATAL_ERROR "Unsupported OS in ZIG_TARGET: ${_os}")
    endif()

    set(CMAKE_CROSSCOMPILING ${_is_cross_compiling})

    if(_arch STREQUAL "x86_64")
        set(CMAKE_SYSTEM_PROCESSOR x86_64)
    elseif(_arch STREQUAL "aarch64")
        set(CMAKE_SYSTEM_PROCESSOR aarch64)
    else()
        message(FATAL_ERROR "Unsupported architecture in ZIG_TARGET: ${_arch}")
    endif()

    # Tell cmake the target triple for compiler identification probes.
    # For clang-family compilers (which zig reports as), cmake passes
    # --target= during its own probing, directing zig to use the correct
    # sysroot for implicit include detection.
    set(CMAKE_C_COMPILER_TARGET "${ZIG_TARGET}")
    set(CMAKE_CXX_COMPILER_TARGET "${ZIG_TARGET}")

    # Prevent cmake from searching host paths during cross-compilation.
    # Without these, find_path/find_library/find_package may locate
    # host headers/libraries instead of our cross-compiled ones.
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

    # Pass the target to compiler and linker flags.
    # Write to a marker file so the zig-cc/zig-c++ wrapper scripts can inject
    # -target automatically for ALL build steps (including ExternalProject deps
    # where env vars from cmake configure don't propagate to build steps).
    set(ENV{ZIG_TARGET} "${ZIG_TARGET}")
    file(WRITE "${_wrapper_dir}/.zig-target" "${ZIG_TARGET}")
    # -Wno-error=date-time: zig cc treats __DATE__/__TIME__ as errors for
    # reproducibility; some deps (exiv2) use them.
    # Alpine CI containers provide musl-native /usr/include, so zig cc's
    # unconditional inclusion of /usr/include is harmless.
    set(CMAKE_C_FLAGS_INIT "")
    set(CMAKE_CXX_FLAGS_INIT "-Wno-error=date-time")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-target ${ZIG_TARGET}")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "-target ${ZIG_TARGET}")

    # Static linking for musl targets
    if(_abi STREQUAL "musl")
        set(ZIG_STATIC_BUILD ON)
        # musl provides pthread, dl, rt within libc — no separate libraries needed
        set(CMAKE_EXE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT} -static")
        message(STATUS "musl target detected — building fully static binary")
    endif()

    # Tell CMake not to test the compiler with a full link (cross-compile checks can be tricky)
    if(_is_cross_compiling)
        set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    endif()

else()
    # =============================================
    # Native build mode (no ZIG_TARGET)
    # =============================================
    # Zig compiles for the host platform. On macOS this gives a normal Mach-O binary,
    # on Linux a normal ELF binary. No cross-compilation flags are set.
    message(STATUS "Zig native build mode (compiling for host)")

    # Remove any stale .zig-target from a previous cross-compilation run
    # so the wrapper scripts don't inject a wrong -target flag.
    if(EXISTS "${_wrapper_dir}/.zig-target")
        file(REMOVE "${_wrapper_dir}/.zig-target")
        message(STATUS "Removed stale .zig-target marker")
    endif()

    # Detect host OS for Homebrew hint on macOS.
    # This helps find ancillary tools/deps used by external dependency builds.
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        # Help CMake find Homebrew prefix.
        execute_process(
            COMMAND brew --prefix
            OUTPUT_VARIABLE HOMEBREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(HOMEBREW_PREFIX)
            message(STATUS "Homebrew prefix: ${HOMEBREW_PREFIX}")
            # Add Homebrew paths for ancillary tools/libraries when present.
            list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")
        endif()
    endif()
endif()

# --- Expose variables for the main CMakeLists.txt ---
set(ZIG_TOOLCHAIN TRUE CACHE BOOL "Building with Zig toolchain" FORCE)
set(ZIG_STATIC_BUILD "${ZIG_STATIC_BUILD}" CACHE BOOL "Fully static musl build" FORCE)
if(DEFINED ZIG_TARGET)
    set(ZIG_TARGET "${ZIG_TARGET}" CACHE STRING "Zig target triple" FORCE)
endif()

# --- Helper variables for ExternalProject autotools deps ---
# Autotools CONFIGURE_COMMAND and BUILD_COMMAND need explicit CC/CFLAGS.
# When cross-compiling, CFLAGS must include -target so the compiler uses
# the correct sysroot (e.g., musl headers instead of glibc).
set(ZIG_EP_CFLAGS "" CACHE STRING "CFLAGS for ExternalProject autotools deps" FORCE)
set(ZIG_EP_CXXFLAGS "" CACHE STRING "CXXFLAGS for ExternalProject autotools deps" FORCE)
set(ZIG_EP_CPP "" CACHE STRING "CPP for ExternalProject autotools deps" FORCE)
set(ZIG_AUTOTOOLS_HOST "" CACHE STRING "Autotools --host for cross-compilation" FORCE)
if(DEFINED ZIG_TARGET AND NOT ZIG_TARGET STREQUAL "")
    # -target: cross-compilation sysroot selection
    # -O2: disables Zig's default UBSan instrumentation that adds __ubsan_* symbols
    set(ZIG_EP_CFLAGS "CFLAGS=-target ${ZIG_TARGET} -O2" CACHE STRING "" FORCE)
    set(ZIG_EP_CXXFLAGS "CXXFLAGS=-target ${ZIG_TARGET} -O2" CACHE STRING "" FORCE)
    # Force autotools to use our zig wrapper for preprocessing instead of
    # /lib/cpp, which would use host glibc headers. The wrapper scripts
    # inject -nostdinc + musl paths for musl targets.
    set(ZIG_EP_CPP "CPP=${CMAKE_C_COMPILER} -E" CACHE STRING "" FORCE)

    # Autotools --host flag for cross-compilation.
    # Without this, autotools configure tries to run compiled test programs,
    # which fails when the target arch differs from the host.
    if(_is_cross_compiling)
        set(ZIG_AUTOTOOLS_HOST "--host=${ZIG_TARGET}" CACHE STRING "Autotools --host for cross-compilation" FORCE)
    endif()
endif()

# --- Helper variables for ExternalProject CMake deps ---
# Cross-compilation: forward the toolchain file so ExternalProject deps
# use the same compiler, target triple, and cross-compilation settings.
# The toolchain file sets CMAKE_<LANG>_COMPILER_TARGET which directs
# cmake's compiler identification probe to use zig's musl sysroot instead
# of discovering host /usr/include as an implicit include directory.
# Native builds: don't forward — the system default compiler is compatible.
set(ZIG_EP_CMAKE_ARGS "" CACHE STRING "CMake args for ExternalProject cmake deps" FORCE)
if(DEFINED ZIG_TARGET AND NOT ZIG_TARGET STREQUAL "")
    set(_ep_cxxflags "-O2 -Wno-error=date-time")
    set(_ep_cflags "-O2")
    set(ZIG_EP_CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DZIG_TARGET=${ZIG_TARGET}
        -DCMAKE_BUILD_TYPE=Release
        "-DCMAKE_C_FLAGS=${_ep_cflags}"
        "-DCMAKE_CXX_FLAGS=${_ep_cxxflags}"
        CACHE STRING "" FORCE)
endif()

message(STATUS "Zig toolchain configured:")
message(STATUS "  Zig executable: ${ZIG_EXECUTABLE}")
message(STATUS "  Zig version: ${ZIG_VERSION}")
if(_zig_native)
    message(STATUS "  Mode: native")
elseif(_is_cross_compiling)
    message(STATUS "  Mode: cross (${ZIG_TARGET})")
else()
    message(STATUS "  Mode: native-targeted (${ZIG_TARGET})")
endif()
message(STATUS "  Static musl build: ${ZIG_STATIC_BUILD}")
