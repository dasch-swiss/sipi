project(sentry_builder C CXX)
include(ExternalProject)

#
# get sentry https://docs.sentry.io/platforms/native/
#

set(SentryVersion "0.9.0")

# The inproc backend compiles the curl transport, which needs to find libcurl.
# All dependencies (including curl) are built from source into CMAKE_BINARY_DIR/local.
set(_SENTRY_PREFIX_PATH "${CMAKE_BINARY_DIR}/local")

# When cross-compiling with Zig for musl, the inproc backend requires
# libunwind which conflicts with musl headers (glibc/musl type clashes).
# Use the "none" backend for musl static builds — crash reporting still
# works, just without in-process stack unwinding.
if(DEFINED ZIG_STATIC_BUILD AND ZIG_STATIC_BUILD)
    set(_SENTRY_BACKEND "none")
    # sentry 0.9.0 unconditionally sets SENTRY_WITH_LIBUNWIND=TRUE for musl
    # targets (via `set()`, not `option()`), so -D cannot override it.
    # Patch sentry's CMakeLists.txt after download to skip the libunwind
    # check — it's not needed with the "none" backend and the host's
    # glibc-based libunwind headers conflict with zig's musl sysroot.
    set(_SENTRY_PATCH_CMD sed -i "s/set(SENTRY_WITH_LIBUNWIND TRUE)/set(SENTRY_WITH_LIBUNWIND FALSE)/" CMakeLists.txt)
    message(STATUS "Sentry: using 'none' backend for musl static build (patching out libunwind)")
else()
    set(_SENTRY_BACKEND "inproc")
    set(_SENTRY_PATCH_CMD "")
endif()

ExternalProject_Add(project_sentry
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        GIT_REPOSITORY https://github.com/getsentry/sentry-native
        GIT_TAG ${SentryVersion}
        GIT_SUBMODULES_RECURSE FALSE
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/sentry-native
        PATCH_COMMAND ${_SENTRY_PATCH_CMD}
        CMAKE_ARGS ${ZIG_EP_CMAKE_ARGS} -DSENTRY_BACKEND=${_SENTRY_BACKEND} -DSENTRY_BUILD_SHARED_LIBS=OFF -DSENTRY_BUILD_EXAMPLES=OFF -DSENTRY_BUILD_TESTS=OFF -DCMAKE_PREFIX_PATH=${_SENTRY_PREFIX_PATH} -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
)
ExternalProject_Get_Property(project_sentry install_dir)

add_library(sentry STATIC IMPORTED GLOBAL)
set_property(TARGET sentry PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libsentry${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(project_sentry project_curl)
add_dependencies(sentry project_sentry)
