project(zstd_builder C CXX)
include(ExternalProject)

include_directories(${CMAKE_BINARY_DIR}/local/include)
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

# Local vendor fallback — offline builds when archive present
# Pinned to v1.5.7 release (was tracking mutable dev branch)
set(_local "${COMMON_VENDOR}/${DEP_ZSTD_FILENAME}")
if(EXISTS "${_local}")
    set(_zstd_url "${_local}")
else()
    set(_zstd_url "${DEP_ZSTD_URL}")
endif()

ExternalProject_Add(project_zstd
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_zstd_url}"
        URL_HASH SHA256=${DEP_ZSTD_SHA256}
        DOWNLOAD_DIR ""
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/zstd-${DEP_ZSTD_VERSION}
        SOURCE_SUBDIR build/cmake
        CMAKE_ARGS ${ZIG_EP_CMAKE_ARGS}
        -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_zstd install_dir)

add_library(zstd STATIC IMPORTED GLOBAL)
set_property(TARGET zstd PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libzstd${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(zstd project_zstd)
