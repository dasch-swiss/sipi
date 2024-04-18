project(tiff_builder C CXX)
include(ExternalProject)

set(LibTiffVersion "4.1.0")

ExternalProject_Add(project_tiff
        DEPENDS project_jbigkit
        DEPENDS project_jpeg
        # DEPENDS project_xz
        DEPENDS project_webp
        # DEPENDS project_zstd
        # DEPENDS project_png

        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        TEST_BEFORE_INSTALL 1
        URL https://download.osgeo.org/libtiff/tiff-${LibTiffVersion}.tar.gz
        URL_HASH SHA256=5d29f32517dadb6dbcd1255ea5bbc93a2b54b94fbf83653b4d65c7d6775b8634
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/libtiff-${LibTiffVersion}
        PATCH_COMMAND patch ${CMAKE_BINARY_DIR}/local/extsrcs/libtiff-${LibTiffVersion}/libtiff/tif_dirinfo.c < ${COMMON_PATCHES}/tiff-4.0.4.patch
        CMAKE_ARGS  -DBUILD_SHARED_LIBS=OFF
                    -DCMAKE_FIND_FRAMEWORK=NEVER
                    -DCMAKE_C_STANDARD_LIBRARIES="-lpthread"
                    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
                    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
                    -DCMAKE_INSTALL_LIBDIR=${CMAKE_BINARY_DIR}/local/lib
                    -DWEBP_LIBRARY=${CMAKE_BINARY_DIR}/local/lib/libwebp${CMAKE_STATIC_LIBRARY_SUFFIX}
                    -DWEBP_INCLUDE_DIR=${CMAKE_BINARY_DIR}/local/include
        INSTALL_COMMAND ${CMAKE_MAKE_PROGRAM} test
        COMMAND ${CMAKE_MAKE_PROGRAM} install
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/local/extsrcs/libtiff-${LibTiffVersion}/libtiff/tif_dir.h ${CMAKE_BINARY_DIR}/local/include/
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_tiff install_dir)

set(TIFF_LIBRARY "libtiff${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(TIFFXX_LIBRARY "libtiffxx${CMAKE_STATIC_LIBRARY_SUFFIX}")

add_library(tiff STATIC IMPORTED GLOBAL)
set_property(TARGET tiff PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libtiff${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(tiff project_tiff xz zstd png)
