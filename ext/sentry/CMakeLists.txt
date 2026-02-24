project(sentry_builder C CXX)
include(ExternalProject)

#
# get sentry https://docs.sentry.io/platforms/native/
#

set(SentryVersion "0.9.0")

# The inproc backend compiles the curl transport, which needs to find libcurl.
# Derive the curl prefix from CURL_INCLUDE_DIR (set by find_package(CURL) in the parent).
if (CURL_INCLUDE_DIR)
    get_filename_component(_CURL_PREFIX "${CURL_INCLUDE_DIR}" DIRECTORY)
    set(_SENTRY_PREFIX_PATH "${CMAKE_BINARY_DIR}/local;${_CURL_PREFIX}")
else()
    set(_SENTRY_PREFIX_PATH "${CMAKE_BINARY_DIR}/local")
endif()

ExternalProject_Add(project_sentry
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        GIT_REPOSITORY https://github.com/getsentry/sentry-native
        GIT_TAG ${SentryVersion}
        GIT_SUBMODULES_RECURSE FALSE
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/sentry-native
        CMAKE_ARGS -DSENTRY_BACKEND=inproc -DSENTRY_BUILD_SHARED_LIBS=OFF -DCMAKE_PREFIX_PATH=${_SENTRY_PREFIX_PATH} -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
)
ExternalProject_Get_Property(project_sentry install_dir)

add_library(sentry STATIC IMPORTED GLOBAL)
set_property(TARGET sentry PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libsentry${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(sentry project_sentry)
