project(kakadu_builder C CXX)
include(ExternalProject)

message(STATUS ${CMAKE_BINARY_DIR}/local/lib)
#
# compiling kakadu (without JAVA interface)
#

message(STATUS "VENDOR=${COMMON_VENDOR}")
message(STATUS "KDU_MAKE=${KDU_MAKE}")

set(KakaduVersion "v8_5-01382N")

## patching the makefiles for zig cross-compilation
if(DEFINED ZIG_STATIC_BUILD AND ZIG_STATIC_BUILD)
    # Zig cross-compilation: patch GCC-specific flags that zig doesn't support,
    # remove -z noexecstack (unsupported by zig's linker), and override CC/AR
    # so Kakadu targets musl.
    set(_kdu_src "${CMAKE_BINARY_DIR}/local/extsrcs/${KakaduVersion}")

    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        # ARM64: remove GCC-style -march= flags entirely — zig's -target already
        # implies the correct ISA and doesn't support -march or -mcpu for aarch64.
        # Also disable HAVE_ARM81 to avoid v8.1-a-specific codegen that zig can't handle.
        set(_kdu_makefiles
            ${_kdu_src}/coresys/make/Makefile-Linux-arm-64-gcc
            ${_kdu_src}/apps/make/Makefile-Linux-arm-64-gcc
            ${_kdu_src}/managed/make/Makefile-Linux-arm-64-gcc
        )
        set(PATCH_COMMAND_OVERRIDE
            sed -i "s/-march=armv8\\.1-a//g" ${_kdu_makefiles}
            COMMAND sed -i "s/-march=armv8-a//g" ${_kdu_makefiles}
            COMMAND sed -i "s/^HAVE_ARM81 = 1/# HAVE_ARM81 = 1/" ${_kdu_makefiles}
            COMMAND sed -i "s|-z noexecstack||" ${_kdu_makefiles}
            COMMAND sed -i "s|CC = g++|CC = ${CMAKE_CXX_COMPILER}|" ${_kdu_makefiles}
            COMMAND sed -i "s|AR = ar|AR = ${CMAKE_AR}|" ${_kdu_makefiles}
        )
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        # x86_64: remove GCC-specific flags that zig/clang doesn't support
        set(_kdu_makefiles
            ${_kdu_src}/coresys/make/Makefile-Linux-x86-64-gcc
            ${_kdu_src}/apps/make/Makefile-Linux-x86-64-gcc
            ${_kdu_src}/managed/make/Makefile-Linux-x86-64-gcc
        )
        set(PATCH_COMMAND_OVERRIDE
            sed -i "s/-mno-avx256-split-unaligned-load//g" ${_kdu_makefiles}
            COMMAND sed -i "s/-mno-avx256-split-unaligned-store//g" ${_kdu_makefiles}
            COMMAND sed -i "s|-z noexecstack||" ${_kdu_makefiles}
            COMMAND sed -i "s|CC = g++|CC = ${CMAKE_CXX_COMPILER}|" ${_kdu_makefiles}
            COMMAND sed -i "s|AR = ar|AR = ${CMAKE_AR}|" ${_kdu_makefiles}
        )
    endif()

    # Use cmake -E env to set CC/AR as environment variables.
    # MAKEFLAGS=-e tells make (and ALL sub-makes) to let environment
    # variables override Makefile-defined variables. This is critical because
    # Kakadu's top-level Makefile calls sub-makes with 'make' (not $(MAKE)),
    # so command-line variable overrides don't propagate — but MAKEFLAGS
    # and environment variables DO propagate through the entire hierarchy.
    set(_KDU_BUILD_PREFIX ${CMAKE_COMMAND} -E env "CC=${CMAKE_CXX_COMPILER}" "AR=${CMAKE_AR}" "MAKEFLAGS=-e")
    set(_KDU_CC_OVERRIDE "")
elseif (CMAKE_CXX_COMPILER_ID MATCHES "(Apple)?[Cc]lang")
    # when we compile with clang, we don't need to patch the makefiles
    set(PATCH_COMMAND_OVERRIDE "")
    set(_KDU_BUILD_PREFIX "")
    set(_KDU_CC_OVERRIDE "")
else ()
    # when we compile with gcc, we need to patch the makefiles, so that the target uses GCC instead of Clang
    set(PATCH_COMMAND_OVERRIDE patch ${CMAKE_BINARY_DIR}/local/extsrcs/${KakaduVersion}/coresys/make/Makefile-Mac-arm-64-gcc < ${COMMON_PATCHES}/kakadu-makefile-coresys.patch && patch ${CMAKE_BINARY_DIR}/local/extsrcs/${KakaduVersion}/apps/make/Makefile-Mac-arm-64-gcc < ${COMMON_PATCHES}/kakadu-makefile-apps.patch && patch ${CMAKE_BINARY_DIR}/local/extsrcs/${KakaduVersion}/managed/make/Makefile-Mac-arm-64-gcc < ${COMMON_PATCHES}/kakadu-makefile-managed.patch)
    set(_KDU_BUILD_PREFIX "")
    set(_KDU_CC_OVERRIDE "")
endif ()

ExternalProject_Add(project_kakadu
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        DOWNLOAD_COMMAND unzip -o -d ${CMAKE_BINARY_DIR}/local/extsrcs ${COMMON_VENDOR}/${KakaduVersion}.zip
        DOWNLOAD_EXTRACT_TIMESTAMP OLD
        PATCH_COMMAND ${PATCH_COMMAND_OVERRIDE}
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ${_KDU_BUILD_PREFIX} make -j1 --directory=${CMAKE_BINARY_DIR}/local/extsrcs/${KakaduVersion}/make/ -f ${KDU_MAKE} EXEC_PLATFORM=${KDU_EXEC_PLATFORM} ${_KDU_CC_OVERRIDE} all_but_jni
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/local/extsrcs/${KakaduVersion}/lib/${KDU_ARCH} ${CMAKE_BINARY_DIR}/local/lib
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/local/extsrcs/${KakaduVersion}/bin/${KDU_ARCH} ${CMAKE_BINARY_DIR}/local/bin
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/local/extsrcs/${KakaduVersion}/managed/all_includes ${CMAKE_BINARY_DIR}/local/include
)

ExternalProject_Get_Property(project_kakadu install_dir)

add_library(kdu STATIC IMPORTED GLOBAL)
set_property(TARGET kdu PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libkdu${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(kdu project_kakadu)

add_library(kdu_aux STATIC IMPORTED GLOBAL)
set_property(TARGET kdu_aux PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libkdu_aux${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(kdu_aux project_kakadu)
