project(lcms2_builder C CXX)
include(ExternalProject)

#
# get lcms2
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

ExternalProject_Add(project_lcms2
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://github.com/dasch-swiss/sipi-third-party/raw/main/third-party/lcms2-2.9.tar.gz
        ${timestamp_policy}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        #URL_HASH SHA256=48c6fdf98396fa245ed86e622028caf49b96fa22f3e5734f853f806fbc8e7d20
        #DOWNLOAD_EXTRACT_TIMESTAMP NEW
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/lcms2-2.9
        CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/lcms2-2.9/configure
        --prefix=${CMAKE_BINARY_DIR}/local
        --with-jpeg=${CMAKE_BINARY_DIR}/local
        --with-tiff=${CMAKE_BINARY_DIR}/local
        --libdir=${CMAKE_BINARY_DIR}/local/lib
        BUILD_COMMAND make
        INSTALL_COMMAND make PREFIX=${CMAKE_BINARY_DIR}/local install
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_lcms2 install_dir)

add_library(lcms2 STATIC IMPORTED GLOBAL)
set_property(TARGET lcms2 PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/liblcms2${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(lcms2 project_lcms2)
