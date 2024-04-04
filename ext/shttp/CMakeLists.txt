project(libshttps_builder C CXX)
include(ExternalProject)

#
# httplib
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)


ExternalProject_Add(project_shttps
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        DOWNLOAD_COMMAND cp -r ${SIPI_SOURCE_DIR}/shttps ${CMAKE_BINARY_DIR}/local/extsrcs
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/shttps
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
        INSTALL_COMMAND make install
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_shttps install_dir)

add_library(shttp STATIC IMPORTED GLOBAL)
set_property(TARGET shttp PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libshttp${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(shttp project_shttps)
