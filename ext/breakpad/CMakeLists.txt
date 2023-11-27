cmake_minimum_required(VERSION 3.24.0)
project(breakpad_builder C CXX)
include(ExternalProject)

#
# get breakpad
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)
ExternalProject_Add(
        project_breakpad
        INSTALL_DIR ${COMMON_LOCAL}
        GIT_REPOSITORY https://github.com/google/breakpad.git
        GIT_TAG main
        GIT_SUBMODULES_RECURSE true
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        ${timestamp_policy}
        SOURCE_DIR ${COMMON_SRCS}/breakpad
        CONFIGURE_COMMAND   ${COMMON_SRCS}/breakpad/configure
                            --prefix=${COMMON_LOCAL}
                            --libdir=${CONFIGURE_LIBDIR}
        BUILD_COMMAND make
        INSTALL_COMMAND make PREFIX=${COMMON_LOCAL} install
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_breakpad install_dir)

add_dependencies(project_breakpad project_sentry)
