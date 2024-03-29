project(libfmt_builder C CXX)
include(ExternalProject)

#
# get fmt
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

ExternalProject_Add(project_fmt
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://github.com/fmtlib/fmt/releases/download/6.2.1/fmt-6.2.1.zip
        ${timestamp_policy}
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        DOWNLOAD_EXTRACT_TIMESTAMP OLD
        SOURCE_DIR ${COMMON_SRCS}/fmt-6.2.1
        CMAKE_ARGS -DFMT_DOC=OFF -DCMAKE_PREFIX_PATH=${COMMON_LOCAL} -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL} -DCMAKE_INSTALL_LIBDIR=${CONFIGURE_LIBDIR}
)

ExternalProject_Get_Property(project_fmt install_dir)

add_library(fmt STATIC IMPORTED GLOBAL)
set_property(TARGET fmt PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libfmt${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(fmt project_fmt)
