project(zstd_builder C CXX)
include(ExternalProject)

include_directories(${CMAKE_BINARY_DIR}/local/include)
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

ExternalProject_Add(project_zstd
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://github.com/facebook/zstd/archive/refs/heads/dev.zip
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/zstd
        SOURCE_SUBDIR build/cmake
        CMAKE_ARGS
        -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_zstd install_dir)

add_library(zstd STATIC IMPORTED GLOBAL)
set_property(TARGET zstd PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libzstd${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(zstd project_zstd)
