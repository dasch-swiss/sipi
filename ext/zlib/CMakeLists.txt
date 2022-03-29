cmake_minimum_required(VERSION 3.14.5)
project(zlib_builder C CXX)
include(ExternalProject)

#
# zlib
#
ExternalProject_Add(project_zlib
        INSTALL_DIR ${COMMON_LOCAL}
        URL http://zlib.net/fossils/zlib-1.2.11.tar.gz
        SOURCE_DIR ${COMMON_SRCS}/zlib-1.2.11
        CONFIGURE_COMMAND ${COMMON_SRCS}/zlib-1.2.11/configure
            --prefix=${COMMON_LOCAL}
            --static
            # --shared
            --libdir=${CONFIGURE_LIBDIR}
        BUILD_COMMAND make
	    INSTALL_COMMAND make INSTALL_LIB=${CONFIGURE_LIBDIR} install INSTALL_TOP=${COMMON_LOCAL}
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_zlib install_dir)

if(MAKE_SHARED_SIPI)
    add_library(zlib SHARED IMPORTED GLOBAL)
    set_property(TARGET zlib PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libz${CMAKE_SHARED_LIBRARY_SUFFIX})
else()
    add_library(zlib STATIC IMPORTED GLOBAL)
    set_property(TARGET zlib PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libz${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()

add_dependencies(zlib project_zlib)
