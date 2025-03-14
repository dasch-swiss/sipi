project(jpeg_builder C CXX)
include(ExternalProject)

#
# get libjpeg 1.4.1
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)


ExternalProject_Add(project_jpeg
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://ijg.org/files/jpegsrc.v9f.tar.gz
        ${timestamp_policy}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        #URL_HASH SHA256=650250979303a649e21f87b5ccd02672af1ea6954b911342ea491f351ceb7122
        #DOWNLOAD_EXTRACT_TIMESTAMP OLD
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/libjpeg-v9f
        CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/libjpeg-v9f/configure --prefix=${CMAKE_BINARY_DIR}/local --enable-shared --enable-static --libdir=${CMAKE_BINARY_DIR}/local/lib
        BUILD_COMMAND make
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_jpeg install_dir)

add_library(jpeg STATIC IMPORTED GLOBAL)
set_property(TARGET jpeg PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libjpeg${CMAKE_STATIC_LIBRARY_SUFFIX})
set(CMAKE_CREATE_SHARED "-DBUILD_SHARED_LIBS=OFF")

add_dependencies(jpeg project_jpeg)
