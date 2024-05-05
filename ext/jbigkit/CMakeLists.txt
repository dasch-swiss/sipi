project(jbigkit_builder C CXX)
include(ExternalProject)

#
# get JBIG
#

ExternalProject_Add(project_jbigkit
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://www.cl.cam.ac.uk/~mgk25/jbigkit/download/jbigkit-2.1.tar.gz
        URL_HASH SHA256=de7106b6bfaf495d6865c7dd7ac6ca1381bd12e0d81405ea81e7f2167263d932
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/extsrcs/jbigkit-2.1
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make CC=clang
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/extsrcs/jbigkit-2.1/libjbig/libjbig${CMAKE_STATIC_LIBRARY_SUFFIX} ${CMAKE_BINARY_DIR}/local/lib
        BUILD_IN_SOURCE 1
        LOG_BUILD TRUE
)
ExternalProject_Get_Property(project_jbigkit install_dir)

add_library(jbigkit STATIC IMPORTED GLOBAL)
set_property(TARGET jbigkit PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libjbig${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(jbigkit project_jbigkit)
