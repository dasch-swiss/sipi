project(lcms2_builder C CXX)
include(ExternalProject)

ExternalProject_Add(project_lcms2
        DEPENDS project_tiff
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://github.com/mm2/Little-CMS/releases/download/lcms2.16/lcms2-2.16.tar.gz
        URL_HASH SHA256=d873d34ad8b9b4cea010631f1a6228d2087475e4dc5e763eb81acc23d9d45a51
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/lcms2
        CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/lcms2/configure
        --prefix=${CMAKE_BINARY_DIR}/local
        --with-jpeg=${CMAKE_BINARY_DIR}/local
        --with-tiff=${CMAKE_BINARY_DIR}/local
        --libdir=${CMAKE_BINARY_DIR}/local/lib
        BUILD_COMMAND ${CMAKE_MAKE_PROGRAM}
        INSTALL_COMMAND make PREFIX=${CMAKE_BINARY_DIR}/local install
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_lcms2 install_dir)

add_library(lcms2 STATIC IMPORTED GLOBAL)
add_dependencies(lcms2 project_lcms2)

set_property(TARGET lcms2 PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/liblcms2${CMAKE_STATIC_LIBRARY_SUFFIX})
