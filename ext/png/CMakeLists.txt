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
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://sourceforge.net/projects/libpng/files/libpng16/${LibPngVersion}/libpng-${LibPngVersion}.tar.gz/download
        ${timestamp_policy}
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        SOURCE_DIR ${COMMON_SRCS}/libpng-${LibPngVersion}
        CMAKE_ARGS  ${SHARED_OPTION}
        -DCMAKE_PREFIX_PATH=${COMMON_LOCAL}
        -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL}
        -DCMAKE_INSTALL_LIBDIR=${CONFIGURE_LIBDIR}
        -DPNG_ARM_NEON:BOOL=on
        -DPNG_TESTS:BOOL=OFF
        -DPNG_BUILD_ZLIB:BOOL=OFF
        -DZLIB_ROOT:BOOL=ON
        -DZLIB_INCLUDE_DIR=${CONFIGURE_INCDIR}
        -DZLIB_LIBRARY=${CONFIGURE_LIBDIR}/libz${CMAKE_STATIC_LIBRARY_SUFFIX}
)

ExternalProject_Get_Property(project_png install_dir)

# static
add_library(png STATIC IMPORTED GLOBAL)
set_property(TARGET png PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libpng${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(png project_png)
