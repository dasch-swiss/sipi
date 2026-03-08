project(libxz_builder C CXX)
include(ExternalProject)

#
# libxyz
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_XZ_FILENAME}")
if(EXISTS "${_local}")
    set(_xz_url "${_local}")
else()
    set(_xz_url "${DEP_XZ_URL}")
endif()

ExternalProject_Add(project_xz
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_xz_url}"
        URL_HASH SHA256=${DEP_XZ_SHA256}
        DOWNLOAD_DIR ""
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/xz-${DEP_XZ_VERSION}
        CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/xz-${DEP_XZ_VERSION}/configure
        --prefix=${CMAKE_BINARY_DIR}/local
        --libdir=${CMAKE_BINARY_DIR}/local/lib
        --enable-static
        --disable-shared
        ${ZIG_AUTOTOOLS_HOST}
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        ${ZIG_EP_CPP}
        ${ZIG_EP_CFLAGS}
        ${ZIG_EP_CXXFLAGS}
        BUILD_COMMAND make
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_xz install_dir)

add_library(xz STATIC IMPORTED GLOBAL)
set_property(TARGET xz PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/liblzma${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(xz project_xz)
