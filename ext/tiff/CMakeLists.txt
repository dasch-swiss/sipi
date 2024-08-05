project(tiff_builder C CXX)
include(ExternalProject)

# get libtiff-4.0.6

set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

set(LibTiffVersion "4.6.0")

ExternalProject_Add(project_tiff
        DEPENDS project_jpeg
        DEPENDS project_xz
        DEPENDS project_webp
        DEPENDS project_zstd
        DEPENDS project_png
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        TEST_BEFORE_INSTALL 1
        URL https://download.osgeo.org/libtiff/tiff-${LibTiffVersion}.tar.gz
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/libtiff-${LibTiffVersion}
        CMAKE_ARGS  -DBUILD_SHARED_LIBS=OFF
                    -DCMAKE_FIND_FRAMEWORK=NEVER
                    -DCMAKE_C_STANDARD_LIBRARIES="-lpthread"
                    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
                    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
                    -DCMAKE_INSTALL_LIBDIR=${CMAKE_BINARY_DIR}/local/lib
                    -DWEBP_LIBRARY=${CMAKE_BINARY_DIR}/local/lib/libwebp${CMAKE_STATIC_LIBRARY_SUFFIX}
                    -DWEBP_INCLUDE_DIR=${CMAKE_BINARY_DIR}/local/include
        INSTALL_COMMAND make test
        COMMAND make install
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/local/extsrcs/libtiff-${LibTiffVersion}/libtiff/tif_dir.h ${CMAKE_BINARY_DIR}/local/include/
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_tiff install_dir)

SET(TIFF_LIBRARY "libtiff${CMAKE_STATIC_LIBRARY_SUFFIX}")
SET(TIFFXX_LIBRARY "libtiffxx${CMAKE_STATIC_LIBRARY_SUFFIX}")

add_library(tiff STATIC IMPORTED GLOBAL)
MESSAGE(STATUS "install dir" ${install_dir})
set_property(TARGET tiff PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libtiff${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(tiff project_tiff)
