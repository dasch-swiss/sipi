cmake_minimum_required(VERSION 3.14.5)
project(expat_builder C CXX)
include(ExternalProject)

ExternalProject_Add(project_expat
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://github.com/libexpat/libexpat/releases/download/R_2_2_9/expat-2.2.9.tar.bz2
        DOWNLOAD_EXTRACT_TIMESTAMP NEW
        SOURCE_DIR ${COMMON_SRCS}/expat
        CONFIGURE_COMMAND ${COMMON_SRCS}/expat/configure
            --prefix=${COMMON_LOCAL}
            --libdir=${CONFIGURE_LIBDIR}
        BUILD_COMMAND make
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_expat install_dir)

if(MAKE_SHARED_SIPI)
    add_library(expat SHARED IMPORTED GLOBAL)
    set_property(TARGET expat PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libexpat${CMAKE_SHARED_LIBRARY_SUFFIX})
else()
    add_library(expat STATIC IMPORTED GLOBAL)
    set_property(TARGET expat PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libexpat${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()

add_dependencies(expat project_expat)


