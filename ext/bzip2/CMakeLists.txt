cmake_minimum_required(VERSION 3.14.5)
project(bzip2_builder C CXX)
include(ExternalProject)

#
# get project_bzip2 and build as a static library
# since we also need this as a shared library, we need to compile with PIC - Position Independent Code
# which may generate less efficient code
#
ExternalProject_Add(project_bzip2
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://sourceforge.net/projects/bzip2/files/latest/download
        DOWNLOAD_EXTRACT_TIMESTAMP NEW
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


#
# get project_bzip2 and build as a shared library
# since we also need this as a shared library, we need to compile with PIC - Position Independent Code
# which may generate less efficient code
#
# there is no install target, thus the copy hack
#
#ExternalProject_Add(project_bzip2_shared
#        INSTALL_DIR ${COMMON_LOCAL}
#        URL https://sourceforge.net/projects/bzip2/files/latest/download
#        SOURCE_DIR ${COMMON_SRCS}/bzip2
#        CONFIGURE_COMMAND ""
#        BUILD_COMMAND make -f Makefile-libbz2_so PREFIX=${COMMON_LOCAL}
#        INSTALL_COMMAND cp -f libbz2.so.1.0.6 ${COMMON_LOCAL}/lib/libbz2${CMAKE_SHARED_LIBRARY_SUFFIX} && chmod a+r ${COMMON_LOCAL}/lib/libbz2${CMAKE_SHARED_LIBRARY_SUFFIX}
#        BUILD_IN_SOURCE 1
#)
#
#ExternalProject_Get_Property(project_bzip2_shared install_dir)
#
#add_library(bzip2_shared SHARED IMPORTED GLOBAL)
#set_property(TARGET bzip2_shared PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libbz2${CMAKE_SHARED_LIBRARY_SUFFIX})
#
#add_dependencies(bzip2_shared project_bzip2_shared)


