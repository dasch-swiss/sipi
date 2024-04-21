project(tiff_builder C CXX)
include(FetchContent)

# FetchContent configuration for libtiff
FetchContent_Declare(
        libtiff
        URL https://download.osgeo.org/libtiff/tiff-4.1.0.tar.gz
        URL_HASH SHA256=5d29f32517dadb6dbcd1255ea5bbc93a2b54b94fbf83653b4d65c7d6775b8634
)

# Check if libtiff has been fetched and if not, populate it
FetchContent_GetProperties(libtiff)
if(NOT libtiff_POPULATED)
    FetchContent_Populate(libtiff)
    # Using ExternalProject to build libtiff due to patching
    include(ExternalProject)
    ExternalProject_Add(
            project_tiff
            SOURCE_DIR ${libtiff_SOURCE_DIR}
            PATCH_COMMAND patch ${libtiff_SOURCE_DIR}/libtiff/tif_dirinfo.c < ${COMMON_PATCHES}/tiff-4.0.4.patch
            CMAKE_ARGS
            -DBUILD_SHARED_LIBS=OFF
            -DCMAKE_FIND_FRAMEWORK=NEVER
            -DCMAKE_C_STANDARD_LIBRARIES="-lpthread"
            -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
            -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
            -DCMAKE_INSTALL_LIBDIR=${CMAKE_BINARY_DIR}/local/lib
            -DWEBP_LIBRARY=${CMAKE_BINARY_DIR}/local/lib/libwebp${CMAKE_STATIC_LIBRARY_SUFFIX}
            -DWEBP_INCLUDE_DIR=${CMAKE_BINARY_DIR}/local/include
            BUILD_COMMAND make
            INSTALL_COMMAND make install
            BUILD_IN_SOURCE 1
    )
endif()

# Define the tiff library as an IMPORTED target
add_library(tiff STATIC IMPORTED GLOBAL)
set_target_properties(tiff PROPERTIES
        IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libtiff${CMAKE_STATIC_LIBRARY_SUFFIX}
)

# Ensure that the tiff library target depends on the completed build of libtiff
add_dependencies(tiff project_tiff xz libzstd_static png_static jbigkit webp jpeg)
