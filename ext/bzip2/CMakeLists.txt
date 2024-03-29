project(bzip2_builder C CXX)
include(ExternalProject)

#
# get project_bzip2 and build as a static library
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

ExternalProject_Add(project_bzip2
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://sourceforge.net/projects/bzip2/files/latest/download
        ${timestamp_policy}
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        SOURCE_DIR ${COMMON_SRCS}/bzip2
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make PREFIX=${COMMON_LOCAL}
        INSTALL_COMMAND make PREFIX=${COMMON_LOCAL} install
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_bzip2 install_dir)

add_library(bzip2 STATIC IMPORTED GLOBAL)
set_property(TARGET bzip2 PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libbz2${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(bzip2 project_bzip2)
