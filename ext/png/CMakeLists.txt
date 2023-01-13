cmake_minimum_required(VERSION 3.14.5)
project(libpng_builder C CXX)
include(ExternalProject)

#
# get png
#
ExternalProject_Add(project_png
        DEPENDS zlib
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://github.com/dasch-swiss/sipi-third-party/raw/main/third-party/libpng-1.6.34.tar.gz
        URL_HASH SHA256=574623a4901a9969080ab4a2df9437026c8a87150dfd5c235e28c94b212964a7
        DOWNLOAD_EXTRACT_TIMESTAMP NEW
        DOWNLOAD_NAME libpng-latest.tar.gz
        #URL https://sourceforge.net/projects/libpng/files/latest/download?source=files
        SOURCE_DIR ${COMMON_SRCS}/libpng-latest
        CMAKE_ARGS  -DPNG_SHARED:BOOL=OFF
                    -DCMAKE_PREFIX_PATH=${COMMON_LOCAL}
                    -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL}
                    -DCMAKE_INSTALL_LIBDIR=${CONFIGURE_LIBDIR}
                    -DPNG_BUILD_ZLIB:BOOL=ON
                    -DZLIB_INCLUDE_DIR=${CONFIGURE_INCDIR}
                    -DZLIB_LIBRARY=${CONFIGURE_LIBDIR}/libz${CMAKE_STATIC_LIBRARY_SUFFIX}
)

ExternalProject_Get_Property(project_png install_dir)

# static
add_library(png STATIC IMPORTED GLOBAL)
set_property(TARGET png PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libpng${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(png project_png)

# shared
add_library(png_shared SHARED IMPORTED GLOBAL)
set_property(TARGET png_shared PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libpng${CMAKE_SHARED_LIBRARY_SUFFIX})

add_dependencies(png_shared project_png)
