project(zlib_builder C CXX)
include(ExternalProject)

#
# zlib
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)


ExternalProject_Add(project_zlib
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL http://zlib.net/zlib-1.3.1.tar.gz
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/zlib
        CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/zlib/configure
        --prefix=${CMAKE_BINARY_DIR}/local
        --static
        # --shared
        --libdir=${CMAKE_BINARY_DIR}/local/lib
        BUILD_COMMAND make
        INSTALL_COMMAND make INSTALL_LIB=${CMAKE_BINARY_DIR}/local/lib install INSTALL_TOP=${CMAKE_BINARY_DIR}/local
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_zlib install_dir)

add_library(zlib STATIC IMPORTED GLOBAL)
set_property(TARGET zlib PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libz${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(zlib project_zlib)
