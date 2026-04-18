project(tiff_builder C CXX)
include(ExternalProject)

# get libtiff-4.0.6

set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

set(TIFF_STATIC_LIBLZMA "${CMAKE_BINARY_DIR}/local/lib/liblzma${CMAKE_STATIC_LIBRARY_SUFFIX}")

# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_TIFF_FILENAME}")
if(EXISTS "${_local}")
    set(_tiff_url "${_local}")
else()
    set(_tiff_url "${DEP_TIFF_URL}")
endif()

# On musl, pthread is part of libc — zig's musl sysroot has no libpthread.a
# and resolves -lpthread to a shared stub, which breaks -static linking.
if(DEFINED ZIG_STATIC_BUILD AND ZIG_STATIC_BUILD)
    set(_TIFF_STANDARD_LIBS "")
    # libtiff builds `tiff_mkg3states` (a Huffman-table codegen helper)
    # inside `if(NOT CMAKE_CROSSCOMPILING)`. In "native-targeted" zig
    # mode CMake sees the same arch (aarch64/x86_64) for host and
    # target, so CMAKE_CROSSCOMPILING is FALSE and the executable is
    # built. Zig's musl sysroot then fails to static-link it (musl
    # libpthread is a shared stub; static needs it in libc.a).
    #
    # CMAKE_CROSSCOMPILING is computed from CMAKE_SYSTEM_NAME vs
    # CMAKE_HOST_SYSTEM_NAME and can't be forced via -D. Instead,
    # patch the guard out — the generated file (tif_fax3sm.c) is
    # already pre-generated in the 4.7.0 release tarball.
    set(_TIFF_PATCH_CMD sed -i "s/if(NOT CMAKE_CROSSCOMPILING)/if(FALSE)/" libtiff/CMakeLists.txt)
else()
    set(_TIFF_STANDARD_LIBS "-lpthread")
    set(_TIFF_PATCH_CMD "")
endif()

ExternalProject_Add(project_tiff
        DEPENDS project_jpeg
        DEPENDS project_xz
        DEPENDS project_webp
        DEPENDS project_zstd
        DEPENDS project_png
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        TEST_COMMAND ""
        URL "${_tiff_url}"
        URL_HASH SHA256=${DEP_TIFF_SHA256}
        DOWNLOAD_DIR ""
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/libtiff-${DEP_TIFF_VERSION}
        PATCH_COMMAND ${_TIFF_PATCH_CMD}
        CMAKE_ARGS  ${ZIG_EP_CMAKE_ARGS} -DBUILD_SHARED_LIBS=OFF
                    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
                    -DCMAKE_FIND_FRAMEWORK=NEVER
                    -DCMAKE_FIND_LIBRARY_SUFFIXES=${CMAKE_STATIC_LIBRARY_SUFFIX}
                    -DCMAKE_C_STANDARD_LIBRARIES=${_TIFF_STANDARD_LIBS}
                    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
                    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
                    -DCMAKE_INSTALL_LIBDIR=${CMAKE_BINARY_DIR}/local/lib
                    -DWEBP_LIBRARY=${CMAKE_BINARY_DIR}/local/lib/libwebp${CMAKE_STATIC_LIBRARY_SUFFIX}
                    -DWEBP_INCLUDE_DIR=${CMAKE_BINARY_DIR}/local/include
                    -DLIBLZMA_LIBRARY_RELEASE=${TIFF_STATIC_LIBLZMA}
                    -DLIBLZMA_LIBRARY_DEBUG=${TIFF_STATIC_LIBLZMA}
                    -Dlibdeflate:BOOL=FALSE
                    -Dtiff-tools=OFF
                    -Dtiff-tests=OFF
                    -Dtiff-contrib=OFF
                    -Dtiff-docs=OFF
        BUILD_COMMAND make -j4
        INSTALL_COMMAND make -j1 install
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/local/extsrcs/libtiff-${DEP_TIFF_VERSION}/libtiff/tif_dir.h ${CMAKE_BINARY_DIR}/local/include/
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_tiff install_dir)

SET(TIFF_LIBRARY "libtiff${CMAKE_STATIC_LIBRARY_SUFFIX}")
SET(TIFFXX_LIBRARY "libtiffxx${CMAKE_STATIC_LIBRARY_SUFFIX}")

add_library(tiff STATIC IMPORTED GLOBAL)
MESSAGE(STATUS "install dir" ${install_dir})
set_property(TARGET tiff PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libtiff${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(tiff project_tiff)
