cmake_minimum_required(VERSION 3.14.5)
project(zstd_builder C CXX)
include(ExternalProject)

include_directories(
        ${COMMON_LOCAL}/include
        /usr/include
        /usr/local/include
)

ExternalProject_Add(project_zstd
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://github.com/facebook/zstd/archive/refs/heads/dev.zip
        DOWNLOAD_EXTRACT_TIMESTAMP NEW
        SOURCE_DIR ${COMMON_SRCS}/zstd
        SOURCE_SUBDIR build/cmake
        CMAKE_ARGS
            -DCMAKE_PREFIX_PATH=${COMMON_LOCAL}
            -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL}
        BUILD_IN_SOURCE 1
        )
ExternalProject_Get_Property(project_zstd install_dir)

if(MAKE_SHARED_SIPI)
    add_library(zstd SHARED IMPORTED GLOBAL)
    set_property(TARGET zstd PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libzstd${CMAKE_SHARED_LIBRARY_SUFFIX})
else()
    add_library(zstd STATIC IMPORTED GLOBAL)
    set_property(TARGET zstd PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libzstd${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()
add_dependencies(zstd project_zstd)
