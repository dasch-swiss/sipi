project(libmagic_builder C CXX)
include(ExternalProject)

#
# libmagic (file) - built from source as a static library
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

find_program(LIBMAGIC_AUTORECONF_EXECUTABLE NAMES autoreconf REQUIRED)
find_program(LIBMAGIC_LIBTOOLIZE_EXECUTABLE NAMES glibtoolize libtoolize REQUIRED)
get_filename_component(_libtoolize_realpath "${LIBMAGIC_LIBTOOLIZE_EXECUTABLE}" REALPATH)
get_filename_component(_libtoolize_bindir "${_libtoolize_realpath}" DIRECTORY)
get_filename_component(_libtoolize_prefix "${_libtoolize_bindir}" DIRECTORY)
set(LIBMAGIC_ACLOCAL_PATH "${_libtoolize_prefix}/share/aclocal")

if(NOT EXISTS "${LIBMAGIC_ACLOCAL_PATH}/libtool.m4")
  set(LIBMAGIC_ACLOCAL_PATH "")
endif()

set(LIBMAGIC_AUTORECONF_ENV
    ${CMAKE_COMMAND}
    -E
    env
    LIBTOOLIZE=${LIBMAGIC_LIBTOOLIZE_EXECUTABLE}
)

if(LIBMAGIC_ACLOCAL_PATH)
  list(APPEND LIBMAGIC_AUTORECONF_ENV ACLOCAL_PATH=${LIBMAGIC_ACLOCAL_PATH})
endif()

list(APPEND LIBMAGIC_AUTORECONF_ENV ${LIBMAGIC_AUTORECONF_EXECUTABLE} -fi)

ExternalProject_Add(project_libmagic
    DEPENDS project_bzip2 project_xz project_zlib project_zstd
    INSTALL_DIR ${CMAKE_BINARY_DIR}/local
    URL "https://github.com/file/file/archive/refs/tags/FILE5_46.tar.gz"
    DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
    ${timestamp_policy}
    SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/libmagic
    # Regenerate autotools files with the active libtool stack.
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/local/extsrcs/libmagic ${LIBMAGIC_AUTORECONF_ENV}
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/local/extsrcs/libmagic ./configure
        --prefix=${CMAKE_BINARY_DIR}/local
        --libdir=${CMAKE_BINARY_DIR}/local/lib
        --enable-static
        --disable-shared
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
    BUILD_COMMAND make -j4
    BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_libmagic install_dir)

add_library(magic_static STATIC IMPORTED GLOBAL)
set_property(TARGET magic_static PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libmagic${CMAKE_STATIC_LIBRARY_SUFFIX})
set_property(TARGET magic_static PROPERTY INTERFACE_LINK_LIBRARIES "bzip2;xz;zlib;zstd")

add_dependencies(magic_static project_libmagic)
