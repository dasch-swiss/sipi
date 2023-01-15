cmake_minimum_required(VERSION 3.19)
project(libmagic_builder C CXX)
include(ExternalProject)

#
# get libmagic (file)
#
ExternalProject_Add(project_magic
        DEPENDS xz
        DEPENDS bzip2
        INSTALL_DIR ${COMMON_LOCAL}
        URL http://ftp.astron.com/pub/file/file-5.41.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP NEW
        SOURCE_DIR ${COMMON_SRCS}/file-5.41
        CONFIGURE_COMMAND ${COMMON_SRCS}/file-5.41/configure --enable-static --enable-shared --prefix=${COMMON_LOCAL}
        BUILD_COMMAND make
        INSTALL_COMMAND make INSTALL_LIB=${CONFIGURE_LIBDIR} install INSTALL_TOP=${COMMON_LOCAL}
        BUILD_IN_SOURCE 1)

ExternalProject_Get_Property(project_magic install_dir)

if(MAKE_SHARED_CSERVE)
    add_library(magic SHARED IMPORTED GLOBAL)
    set_property(TARGET magic PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libmagic${CMAKE_SHARED_LIBRARY_SUFFIX})
else()
    add_library(magic STATIC IMPORTED GLOBAL)
    set_property(TARGET magic PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libmagic${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()

add_dependencies(magic project_magic)
