project(bzip2_builder C CXX)
include(ExternalProject)

#
# get project_bzip2 and build as a static library
#

set(Bzip2Version "66c46b8c9436613fd81bc5d03f63a61933a4dcc3") # commit from Jun 01, 2023
ExternalProject_Add(project_bzip2
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        GIT_REPOSITORY https://gitlab.com/bzip2/bzip2
        GIT_TAG ${Bzip2Version}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/bzip2
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DENABLE_SHARED_LIB=OFF -DENABLE_STATIC_LIB=ON -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local -DCMAKE_INSTALL_LIBDIR=${CMAKE_BINARY_DIR}/local/lib
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_bzip2 install_dir)

add_library(bzip2 STATIC IMPORTED GLOBAL)
set_property(TARGET bzip2 PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libbz2_static${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(bzip2 project_bzip2)
