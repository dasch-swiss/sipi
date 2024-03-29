project(libshttps_builder C CXX)
include(ExternalProject)

#
# httplib
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)


ExternalProject_Add(project_shttps
        INSTALL_DIR ${COMMON_LOCAL}
        DOWNLOAD_COMMAND cp -r ${SIPI_SOURCE_DIR}/shttps ${COMMON_SRCS}
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        ${timestamp_policy}
        SOURCE_DIR ${COMMON_SRCS}/shttps
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL}
        INSTALL_COMMAND make install
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_shttps install_dir)

add_library(shttp STATIC IMPORTED GLOBAL)
set_property(TARGET shttp PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libshttp${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(shttp project_shttps)
