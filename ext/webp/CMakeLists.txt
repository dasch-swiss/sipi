# https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.1.0.tar.gz
project(libwebp_builder C CXX)
include(ExternalProject)

#
# libwebp
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

ExternalProject_Add(project_webp
        DEPENDS project_xz
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.1.0.tar.gz
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        ${timestamp_policy}
        #DOWNLOAD_EXTRACT_TIMESTAMP NEW
        SOURCE_DIR ${COMMON_SRCS}/libwebp
        CMAKE_ARGS -DBUILD_SHARED_LIBS=OFF -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_VWEBP=OFF -DCMAKE_PREFIX_PATH=${COMMON_LOCAL} -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL}
        )
ExternalProject_Get_Property(project_webp install_dir)

add_library(webp STATIC IMPORTED GLOBAL)
set_property(TARGET webp PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libwebp${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(webp project_webp)
