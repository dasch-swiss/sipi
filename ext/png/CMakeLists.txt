project(libpng_builder C CXX)
include(ExternalProject)

#
# get png
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

set(LibPngVersion "1.6.41")

ExternalProject_Add(project_png
        DEPENDS project_zlib
        DEPENDS project_xz
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://sourceforge.net/projects/libpng/files/libpng16/${LibPngVersion}/libpng-${LibPngVersion}.tar.gz/download
        ${timestamp_policy}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/libpng-${LibPngVersion}
        CMAKE_ARGS  ${SHARED_OPTION}
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
