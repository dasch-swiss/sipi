project(libfmt_builder C CXX)
include(ExternalProject)

#
# get fmt
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

ExternalProject_Add(project_fmt
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://github.com/fmtlib/fmt/releases/download/6.2.1/fmt-6.2.1.zip
        ${timestamp_policy}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        DOWNLOAD_EXTRACT_TIMESTAMP OLD
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/fmt-6.2.1
        CMAKE_ARGS -DFMT_DOC=OFF -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local -DCMAKE_INSTALL_LIBDIR=${CMAKE_BINARY_DIR}/local/lib
)

ExternalProject_Get_Property(project_fmt install_dir)

add_library(fmt STATIC IMPORTED GLOBAL)
set_property(TARGET fmt PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libfmt${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(fmt project_fmt)
