cmake_minimum_required(VERSION 3.14.5)
project(fontconfig_builder C CXX)
include(ExternalProject)

#
# get fontconfig
#

ExternalProject_Add(project_fontconfig
        DEPENDS zlib
        DEPENDS expat
        DEPENDS png
        DEPENDS freetype
        DEPENDS harfbuzz
        # DEPENDS freetype_shared
        DEPENDS bzip2
        # DEPENDS bzip2_shared
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://www.freedesktop.org/software/fontconfig/release/fontconfig-2.13.92.tar.gz
        SOURCE_DIR ${COMMON_SRCS}/fontconfig-2.13.1
        CONFIGURE_COMMAND   ${COMMON_SRCS}/fontconfig-2.13.1/configure
                            --prefix=${COMMON_LOCAL}
                            FREETYPE_CFLAGS=-I${CONFIGURE_INCDIR}/freetype2
                            FREETYPE_LIBS="-L${CONFIGURE_LIBDIR} -lfreetype -lbz2 -lharfbuzz -lpng -lz -lm"
                            --disable-shared
                            --enable-static
        BUILD_COMMAND make
        INSTALL_COMMAND make PREFIX=${COMMON_LOCAL} install
        BUILD_IN_SOURCE 1
        )

ExternalProject_Get_Property(project_fontconfig install_dir)

# static
add_library(fontconfig STATIC IMPORTED GLOBAL)
set_property(TARGET fontconfig PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libfontconfig${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(fontconfig project_fontconfig)

# shared
add_library(fontconfig_shared SHARED IMPORTED GLOBAL)
set_property(TARGET fontconfig_shared PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libfontconfig${CMAKE_SHARED_LIBRARY_SUFFIX})

add_dependencies(fontconfig_shared project_fontconfig)


