project(sentry_builder C CXX)
include(ExternalProject)

#
# get sentry https://docs.sentry.io/platforms/native/
#

set(SentryVersion "0.9.0")

# The inproc backend compiles the curl transport, which needs to find libcurl.
# All dependencies (including curl) are built from source into CMAKE_BINARY_DIR/local.
set(_SENTRY_PREFIX_PATH "${CMAKE_BINARY_DIR}/local")

ExternalProject_Add(project_sentry
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        GIT_REPOSITORY https://github.com/getsentry/sentry-native
        GIT_TAG ${SentryVersion}
        GIT_SUBMODULES_RECURSE FALSE
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/sentry-native
        CMAKE_ARGS -DSENTRY_BACKEND=inproc -DSENTRY_BUILD_SHARED_LIBS=OFF -DSENTRY_BUILD_EXAMPLES=OFF -DSENTRY_BUILD_TESTS=OFF -DCMAKE_PREFIX_PATH=${_SENTRY_PREFIX_PATH} -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
)
ExternalProject_Get_Property(project_sentry install_dir)

add_library(sentry STATIC IMPORTED GLOBAL)
set_property(TARGET sentry PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libsentry${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(project_sentry project_curl)
add_dependencies(sentry project_sentry)
