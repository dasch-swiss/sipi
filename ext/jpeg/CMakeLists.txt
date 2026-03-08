project(jpeg_builder C CXX)
include(ExternalProject)

#
# get libjpeg 1.4.1
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)


if(ZIG_STATIC_BUILD)
    set(_jpeg_shared_flags --disable-shared --enable-static)
else()
    set(_jpeg_shared_flags --enable-shared --enable-static)
endif()

# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_JPEG_FILENAME}")
if(EXISTS "${_local}")
    set(_jpeg_url "${_local}")
else()
    set(_jpeg_url "${DEP_JPEG_URL}")
endif()

ExternalProject_Add(project_jpeg
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_jpeg_url}"
        URL_HASH SHA256=${DEP_JPEG_SHA256}
        ${timestamp_policy}
        DOWNLOAD_DIR ""
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/libjpeg-v${DEP_JPEG_VERSION}
        CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/libjpeg-v${DEP_JPEG_VERSION}/configure --prefix=${CMAKE_BINARY_DIR}/local ${_jpeg_shared_flags} --libdir=${CMAKE_BINARY_DIR}/local/lib
        ${ZIG_AUTOTOOLS_HOST}
        CC=${CMAKE_C_COMPILER}
        ${ZIG_EP_CPP}
        ${ZIG_EP_CFLAGS}
        BUILD_COMMAND make
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_jpeg install_dir)

add_library(jpeg STATIC IMPORTED GLOBAL)
set_property(TARGET jpeg PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libjpeg${CMAKE_STATIC_LIBRARY_SUFFIX})
set(CMAKE_CREATE_SHARED "-DBUILD_SHARED_LIBS=OFF")

add_dependencies(jpeg project_jpeg)
