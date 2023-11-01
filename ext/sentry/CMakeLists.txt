cmake_minimum_required(VERSION 3.14.5)
project(libsentry_builder C CXX)
include(ExternalProject)

#
# Sentry SDK
#
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.24.0)
    set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)
else()
    unset(timestamp_policy)
endif()


ExternalProject_Add(project_sentry
        DEPENDS curl
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://github.com/getsentry/sentry-native/releases/download/0.6.6/sentry-native.zip
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        ${timestamp_policy}
        #DOWNLOAD_EXTRACT_TIMESTAMP NEW
        SOURCE_DIR ${COMMON_SRCS}/sentry-native
        CMAKE_ARGS -D SENTRY_BACKEND=inproc -DSENTRY_BUILD_SHARED_LIBS=OFF -DCMAKE_PREFIX_PATH=${COMMON_LOCAL} -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL}
        )
ExternalProject_Get_Property(project_sentry install_dir)

add_library(sentry STATIC IMPORTED GLOBAL)
set_property(TARGET sentry PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libsentry${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(sentry project_sentry)
add_dependencies(project_sentry curl)
