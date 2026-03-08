project(libpng_builder C CXX)
include(ExternalProject)

#
# get png
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_PNG_FILENAME}")
if(EXISTS "${_local}")
    set(_png_url "${_local}")
else()
    set(_png_url "${DEP_PNG_URL}")
endif()

ExternalProject_Add(project_png
        DEPENDS project_zlib
        DEPENDS project_xz
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_png_url}"
        URL_HASH SHA256=${DEP_PNG_SHA256}
        ${timestamp_policy}
        DOWNLOAD_DIR ""
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/libpng-${DEP_PNG_VERSION}
        CMAKE_ARGS  ${ZIG_EP_CMAKE_ARGS} ${SHARED_OPTION}
        -DPNG_SHARED=OFF
        -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
        -DCMAKE_INSTALL_LIBDIR=${CMAKE_BINARY_DIR}/local/lib
        -DPNG_ARM_NEON:BOOL=on
        -DPNG_TESTS:BOOL=OFF
        -DPNG_BUILD_ZLIB:BOOL=OFF
        -DZLIB_ROOT:BOOL=ON
        -DZLIB_INCLUDE_DIR=${CMAKE_BINARY_DIR}/local/include
        -DZLIB_LIBRARY=${CMAKE_BINARY_DIR}/local/lib/libz${CMAKE_STATIC_LIBRARY_SUFFIX}
)

ExternalProject_Get_Property(project_png install_dir)

# static
add_library(png STATIC IMPORTED GLOBAL)
set_property(TARGET png PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libpng${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(png project_png)
