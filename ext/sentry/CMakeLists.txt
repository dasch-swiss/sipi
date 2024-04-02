project(sentry_builder C CXX)
include(ExternalProject)

#
# get sentry https://docs.sentry.io/platforms/native/
#

set(SentryVersion "0.7.1")

ExternalProject_Add(project_sentry
        INSTALL_DIR ${COMMON_LOCAL}
        GIT_REPOSITORY https://github.com/getsentry/sentry-native
        GIT_TAG ${SentryVersion}
        GIT_SUBMODULES_RECURSE FALSE
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        ${timestamp_policy}
        SOURCE_DIR ${COMMON_SRCS}/sentry-native
        CMAKE_ARGS -DSENTRY_BACKEND=none -DSENTRY_BUILD_SHARED_LIBS=OFF -DCMAKE_PREFIX_PATH=${COMMON_LOCAL} -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL}
)
ExternalProject_Get_Property(project_sentry install_dir)

add_library(sentry STATIC IMPORTED GLOBAL)
set_property(TARGET sentry PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libsentry${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(sentry project_sentry)
