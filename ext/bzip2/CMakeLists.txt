project(bzip2_builder C CXX)
include(ExternalProject)

#
# get project_bzip2 and build as a static library
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

ExternalProject_Add(project_bzip2
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://gitlab.com/bzip2/bzip2/-/archive/bzip2-1.0.8/bzip2-bzip2-1.0.8.tar.bz2
        ${timestamp_policy}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/bzip2
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make CC=${CMAKE_C_COMPILER} PREFIX=${CMAKE_BINARY_DIR}/local
        INSTALL_COMMAND make CC=${CMAKE_C_COMPILER} PREFIX=${CMAKE_BINARY_DIR}/local install
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_bzip2 install_dir)

add_library(bzip2 STATIC IMPORTED GLOBAL)
set_property(TARGET bzip2 PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libbz2${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(bzip2 project_bzip2)
