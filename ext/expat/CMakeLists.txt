project(expat_builder C CXX)
include(ExternalProject)

set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

ExternalProject_Add(project_expat
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://github.com/libexpat/libexpat/releases/download/R_2_5_0/expat-2.5.0.tar.bz2
        ${timestamp_policy}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/expat
        CMAKE_ARGS
        -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
        -DEXPAT_BUILD_TOOLS:BOOL=OFF
        -DEXPAT_BUILD_EXAMPLES:BOOL=OFF
        -DEXPAT_BUILD_DOCS:BOOL=OFF
        -DBUILD_SHARED_LIBS:BOOL=OFF
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_expat install_dir)

add_library(expat STATIC IMPORTED GLOBAL)
set_property(TARGET expat PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libexpat${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(expat project_expat)
