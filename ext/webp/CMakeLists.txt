project(libwebp_builder C CXX)
include(ExternalProject)

#
# libwebp
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

ExternalProject_Add(project_webp
        DEPENDS project_xz
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.2.0.tar.gz
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        ${timestamp_policy}
        #DOWNLOAD_EXTRACT_TIMESTAMP NEW
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/libwebp
        CMAKE_ARGS  -DBUILD_SHARED_LIBS=OFF
                    -DWEBP_BUILD_ANIM_UTILS=OFF
                    -DWEBP_BUILD_VWEBP=OFF
                    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
                    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
                    -DWEBP_BUILD_CWEBP=OFF
        )
ExternalProject_Get_Property(project_webp install_dir)

add_library(webp STATIC IMPORTED GLOBAL)
set_property(TARGET webp PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libwebp${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(webp project_webp)
