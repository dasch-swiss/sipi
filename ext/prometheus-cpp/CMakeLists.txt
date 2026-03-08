project(prometheus_cpp_builder CXX)
include(ExternalProject)

#
# prometheus-cpp (core only — no pull/push/compression)
# https://github.com/jupp0r/prometheus-cpp
#

set(PrometheusVersion "1.3.0")

ExternalProject_Add(project_prometheus_cpp
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        GIT_REPOSITORY https://github.com/jupp0r/prometheus-cpp.git
        GIT_TAG v${PrometheusVersion}
        GIT_SUBMODULES_RECURSE FALSE
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
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
