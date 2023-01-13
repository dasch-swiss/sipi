cmake_minimum_required(VERSION 3.14.5)
project(libfmt_builder C CXX)
include(ExternalProject)

#
# get fmt
#
ExternalProject_Add(
        project_fmt
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://github.com/fmtlib/fmt/releases/download/6.2.1/fmt-6.2.1.zip
        DOWNLOAD_EXTRACT_TIMESTAMP NEW
        SOURCE_DIR ${COMMON_SRCS}/fmt-6.2.1
        CMAKE_ARGS -DFMT_DOC=OFF -DCMAKE_PREFIX_PATH=${COMMON_LOCAL} -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL} -DCMAKE_INSTALL_LIBDIR=${CONFIGURE_LIBDIR}
)

ExternalProject_Get_Property(project_fmt install_dir)

if(MAKE_SHARED_SIPI)
    add_library(fmt SHARED IMPORTED GLOBAL)
    set_property(TARGET fmt PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libfmt${CMAKE_SHARED_LIBRARY_SUFFIX})
else()
    add_library(fmt STATIC IMPORTED GLOBAL)
    set_property(TARGET fmt PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libfmt${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()

add_dependencies(fmt project_fmt)
