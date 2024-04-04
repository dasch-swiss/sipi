project(libxz_builder C CXX)
include(ExternalProject)

#
# libxyz
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

ExternalProject_Add(project_xz
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://tukaani.org/xz/xz-5.2.5.tar.gz
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/xz-5.2.5
        CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/xz-5.2.5/configure --prefix=${CMAKE_BINARY_DIR}/local --libdir=${CMAKE_BINARY_DIR}/local/lib
        BUILD_COMMAND make
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_xz install_dir)

add_library(xz STATIC IMPORTED GLOBAL)
set_property(TARGET xz PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/liblzma${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(xz project_xz)
