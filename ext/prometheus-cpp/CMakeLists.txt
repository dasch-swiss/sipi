project(prometheus_cpp_builder CXX)
include(ExternalProject)

#
# prometheus-cpp (core only — no pull/push/compression)
# https://github.com/jupp0r/prometheus-cpp
#

# Local vendor fallback — offline builds when archive present
# Converted from GIT_REPOSITORY/GIT_TAG to URL/URL_HASH for reproducibility
set(_local "${COMMON_VENDOR}/${DEP_PROMETHEUS_FILENAME}")
if(EXISTS "${_local}")
    set(_prometheus_url "${_local}")
else()
    set(_prometheus_url "${DEP_PROMETHEUS_URL}")
endif()

ExternalProject_Add(project_prometheus_cpp
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_prometheus_url}"
        URL_HASH SHA256=${DEP_PROMETHEUS_SHA256}
        DOWNLOAD_DIR ""
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/prometheus-cpp
        CMAKE_ARGS ${ZIG_EP_CMAKE_ARGS}
            -DENABLE_PULL=OFF
            -DENABLE_PUSH=OFF
            -DENABLE_COMPRESSION=OFF
            -DENABLE_TESTING=OFF
            -DOVERRIDE_CXX_STANDARD_FLAGS=OFF
            -DUSE_THIRDPARTY_LIBRARIES=OFF
            -DBUILD_SHARED_LIBS=OFF
            -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
)
ExternalProject_Get_Property(project_prometheus_cpp install_dir)

add_library(prometheus_core STATIC IMPORTED GLOBAL)
set_property(TARGET prometheus_core PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libprometheus-cpp-core${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(prometheus_core project_prometheus_cpp)
