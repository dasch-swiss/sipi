project(libwebp_builder C CXX)
include(ExternalProject)

#
# libwebp
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_WEBP_FILENAME}")
if(EXISTS "${_local}")
    set(_webp_url "${_local}")
else()
    set(_webp_url "${DEP_WEBP_URL}")
endif()

ExternalProject_Add(project_webp
        DEPENDS project_xz
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_webp_url}"
        URL_HASH SHA256=${DEP_WEBP_SHA256}
        DOWNLOAD_DIR ""
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/libwebp
        CMAKE_ARGS  ${ZIG_EP_CMAKE_ARGS} -DBUILD_SHARED_LIBS=OFF
                    -DWEBP_BUILD_ANIM_UTILS=OFF
                    -DWEBP_BUILD_VWEBP=OFF
                    -DWEBP_BUILD_WEBPINFO=OFF
                    -DWEBP_BUILD_WEBPMUX=OFF
                    -DWEBP_BUILD_EXTRAS=OFF
                    -DCMAKE_DISABLE_FIND_PACKAGE_TIFF=TRUE
                    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
                    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
                    -DCMAKE_INSTALL_LIBDIR=lib
                    -DWEBP_BUILD_CWEBP=OFF
                    -DWEBP_BUILD_DWEBP=OFF
                    -DWEBP_BUILD_IMG2WEBP=OFF
                    -DWEBP_BUILD_GIF2WEBP=OFF
                    -DCMAKE_DISABLE_FIND_PACKAGE_GIF=TRUE
        )
ExternalProject_Get_Property(project_webp install_dir)

add_library(webp STATIC IMPORTED GLOBAL)
set_property(TARGET webp PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libwebp${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(webp project_webp)
