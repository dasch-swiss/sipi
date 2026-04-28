project(sentry_builder C CXX)
include(ExternalProject)

#
# get sentry https://docs.sentry.io/platforms/native/
#

# The inproc backend compiles the curl transport, which needs to find libcurl.
# All dependencies (including curl) are built from source into CMAKE_BINARY_DIR/local.
set(_SENTRY_PREFIX_PATH "${CMAKE_BINARY_DIR}/local")

set(_SENTRY_BACKEND "inproc")
set(_SENTRY_PATCH_CMD "")

# Local vendor fallback — offline builds when archive present
# Converted from GIT_REPOSITORY/GIT_TAG to URL/URL_HASH for reproducibility
set(_local "${COMMON_VENDOR}/${DEP_SENTRY_FILENAME}")
if(EXISTS "${_local}")
    set(_sentry_url "${_local}")
else()
    set(_sentry_url "${DEP_SENTRY_URL}")
endif()

ExternalProject_Add(project_sentry
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_sentry_url}"
        URL_HASH SHA256=${DEP_SENTRY_SHA256}
        DOWNLOAD_DIR ""
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/sentry-native
        PATCH_COMMAND ${_SENTRY_PATCH_CMD}
        CMAKE_ARGS -DSENTRY_BACKEND=${_SENTRY_BACKEND} -DSENTRY_BUILD_SHARED_LIBS=OFF -DSENTRY_BUILD_EXAMPLES=OFF -DSENTRY_BUILD_TESTS=OFF -DCMAKE_PREFIX_PATH=${_SENTRY_PREFIX_PATH} -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local -DCMAKE_INSTALL_LIBDIR=lib
)
ExternalProject_Get_Property(project_sentry install_dir)

add_library(sentry STATIC IMPORTED GLOBAL)
set_property(TARGET sentry PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libsentry${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(project_sentry project_curl)
add_dependencies(sentry project_sentry)
