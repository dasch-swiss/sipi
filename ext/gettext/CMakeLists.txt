cmake_minimum_required(VERSION 3.14.5)
project(gettext_builder C CXX)
include(ExternalProject)

#
# get gettext
#
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.25.1)
    set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)
else()
    unset(timestamp_policy)
endif()

ExternalProject_Add(
        project_gettext
        INSTALL_DIR ${COMMON_LOCAL}
        URL http://ftp.gnu.org/pub/gnu/gettext/gettext-0.21.1.tar.gz
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        ${timestamp_policy}
        SOURCE_DIR ${COMMON_SRCS}/gettext--0.21.1
        CONFIGURE_COMMAND   ${COMMON_SRCS}/gettext--0.21.1/configure
                            --prefix=${COMMON_LOCAL}
                            --enable-static
                            --libdir=${CONFIGURE_LIBDIR}
                            --disable-java
        BUILD_COMMAND make
        INSTALL_COMMAND make PREFIX=${COMMON_LOCAL} install
        # INSTALL_COMMAND ${CMAKE_COMMAND} -E copy ${COMMON_SRCS}/gettext--0.20.1/gettext-runtime/intl/.libs/libgnuintl${CMAKE_STATIC_LIBRARY_SUFFIX} ${CONFIGURE_LIBDIR}
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_gettext install_dir)

add_library(gettext STATIC IMPORTED GLOBAL)
set_property(TARGET gettext PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libgettextpo${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(gettext project_gettext)

# only needed on mac. under Linux this is not built and needed
if(CMAKE_SYSTEM_NAME STREQUAL DARWIN)
    add_library(gettext_intl STATIC IMPORTED GLOBAL)
    set_property(TARGET gettext_intl PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libintl${CMAKE_STATIC_LIBRARY_SUFFIX})
    add_dependencies(gettext_intl project_gettext)
endif()
