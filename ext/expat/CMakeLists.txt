cmake_minimum_required(VERSION 3.14.5)
project(expat_builder C CXX)
include(ExternalProject)

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.24.0)
    set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)
else()
    unset(timestamp_policy)
endif()


ExternalProject_Add(project_expat
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://github.com/libexpat/libexpat/releases/download/R_2_5_0/expat-2.5.0.tar.bz2
        ${timestamp_policy}
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        SOURCE_DIR ${COMMON_SRCS}/expat
        CMAKE_ARGS
            -DCMAKE_PREFIX_PATH=${COMMON_LOCAL}
            -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL}
            -DEXPAT_BUILD_TOOLS:BOOL=OFF
            -DEXPAT_BUILD_EXAMPLES:BOOL=OFF
            -DEXPAT_BUILD_DOCS:BOOL=OFF
            -DBUILD_SHARED_LIBS:BOOL=${MAKE_SHARED_SIPI}
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_expat install_dir)

if(MAKE_SHARED_SIPI)
    add_library(expat SHARED IMPORTED GLOBAL)
    set_property(TARGET expat PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libexpat${CMAKE_SHARED_LIBRARY_SUFFIX})
else()
    add_library(expat STATIC IMPORTED GLOBAL)
    set_property(TARGET expat PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libexpat${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()

add_dependencies(expat project_expat)
