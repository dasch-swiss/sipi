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

if(DEFINED ZIG_TARGET AND NOT ZIG_TARGET STREQUAL "")
    # =============================================
    # Cross-compilation mode (ZIG_TARGET is set)
    # =============================================
    set(_zig_native OFF)
    message(STATUS "Zig cross-compilation mode: ${ZIG_TARGET}")

    # Parse target triple
    string(REPLACE "-" ";" _target_parts "${ZIG_TARGET}")
    list(GET _target_parts 0 _arch)
    list(GET _target_parts 1 _os)
    list(GET _target_parts 2 _abi)

    # Map OS to CMake system name
    if(_os STREQUAL "linux")
        set(CMAKE_SYSTEM_NAME Linux)
    elseif(_os STREQUAL "macos")
        set(CMAKE_SYSTEM_NAME Darwin)
    else()
        message(FATAL_ERROR "Unsupported OS in ZIG_TARGET: ${_os}")
    endif()

    set(CMAKE_CROSSCOMPILING TRUE)

    if(_arch STREQUAL "x86_64")
        set(CMAKE_SYSTEM_PROCESSOR x86_64)
    elseif(_arch STREQUAL "aarch64")
        set(CMAKE_SYSTEM_PROCESSOR aarch64)
    else()
        message(FATAL_ERROR "Unsupported architecture in ZIG_TARGET: ${_arch}")
    endif()

    # Pass the target to compiler and linker flags
    set(ENV{ZIG_TARGET} "${ZIG_TARGET}")
    set(CMAKE_C_FLAGS_INIT "-target ${ZIG_TARGET}")
    set(CMAKE_CXX_FLAGS_INIT "-target ${ZIG_TARGET}")
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
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

else()
    # =============================================
    # Native build mode (no ZIG_TARGET)
    # =============================================
    # Zig compiles for the host platform. On macOS this gives a normal Mach-O binary,
    # on Linux a normal ELF binary. No cross-compilation flags are set.
    message(STATUS "Zig native build mode (compiling for host)")

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

message(STATUS "Zig toolchain configured:")
message(STATUS "  Zig executable: ${ZIG_EXECUTABLE}")
message(STATUS "  Zig version: ${ZIG_VERSION}")
if(_zig_native)
    message(STATUS "  Mode: native")
else()
    message(STATUS "  Mode: cross (${ZIG_TARGET})")
endif()
message(STATUS "  Static musl build: ${ZIG_STATIC_BUILD}")
