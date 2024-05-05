project(jpeg_builder C CXX)
include(ExternalProject)

ExternalProject_Add(project_jpeg
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://ijg.org/files/jpegsrc.v9f.tar.gz
        URL_HASH SHA256=04705c110cb2469caa79fb71fba3d7bf834914706e9641a4589485c1f832565b
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/libjpeg-v9f
        CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/libjpeg-v9f/configure --prefix=${CMAKE_BINARY_DIR}/local --enable-shared --enable-static --libdir=${CMAKE_BINARY_DIR}/local/lib
        BUILD_COMMAND make
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_jpeg install_dir)

add_library(jpeg STATIC IMPORTED GLOBAL)
set_property(TARGET jpeg PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libjpeg${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(jpeg project_jpeg)
