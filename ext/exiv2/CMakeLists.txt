project(exiv2_builder C CXX)
include(ExternalProject)

include_directories(
        ${CMAKE_BINARY_DIR}/local/include
)

set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

set(Exiv2Version "v0.28.5")


ExternalProject_Add(project_exiv2
        DEPENDS project_expat
        DEPENDS project_png
        DEPENDS project_zlib
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://github.com/Exiv2/exiv2/archive/refs/tags/${Exiv2Version}.tar.gz
        ${timestamp_policy}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/exiv2-trunk
        CMAKE_ARGS -DBUILD_SHARED_LIBS=OFF -DEXIV2_ENABLE_INIH:BOOL=OFF -DEXIV2_ENABLE_BROTLI:BOOL=OFF -DEXIV2_BUILD_SAMPLES:BOOL=OFF -DEXIV2_BUILD_EXIV2_COMMAND=OFF -DEXIV2_ENABLE_NLS=OFF -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local -DCMAKE_INSTALL_LIBDIR=${CMAKE_BINARY_DIR}/local/lib

        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_exiv2 install_dir)

add_library(exiv2 STATIC IMPORTED GLOBAL)
set_property(TARGET exiv2 PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libexiv2${CMAKE_STATIC_LIBRARY_SUFFIX})
add_library(xmp STATIC IMPORTED GLOBAL)
set_property(TARGET xmp PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libexiv2-xmp${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(exiv2 project_exiv2 expat png zlib)
