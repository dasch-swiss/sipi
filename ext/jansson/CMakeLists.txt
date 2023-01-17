cmake_minimum_required(VERSION 3.14.5)
project(libjansson_builder C CXX)
include(ExternalProject)

set(JANSSON_INSTALL_LIB_DIR "lib/${BITS}")

#
# get jansson
#
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.24.0)
    set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)
else()
    unset(timestamp_policy)
endif()

ExternalProject_Add(
        project_jansson
        INSTALL_DIR ${COMMON_LOCAL}
#        GIT_REPOSITORY https://github.com/akheron/jansson.git
        URL http://www.digip.org/jansson/releases/jansson-2.13.1.tar.gz
        ${timestamp_policy}
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        #DOWNLOAD_EXTRACT_TIMESTAMP OLD
        SOURCE_DIR ${COMMON_SRCS}/jansson
        CMAKE_ARGS  -DCMAKE_PREFIX_PATH=${COMMON_LOCAL}
                    -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL}
                    -DJANSSON_INSTALL_LIB_DIR:String=${CONFIGURE_LIBDIR}
)
ExternalProject_Get_Property(project_jansson install_dir)

add_library(jansson STATIC IMPORTED GLOBAL)
set_property(TARGET jansson PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libjansson${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(jansson project_jansson)
