#project(jansson_builder C CXX)
#include(FetchContent)
#
#set(JANSSON_BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared library")
#set(JANSSON_BUILD_DOCS OFF CACHE BOOL "Build documentation")
#set(JANSSON_EXAMPLES OFF CACHE BOOL "Build examples")
#set(JANSSON_WITHOUT_TESTS ON CACHE BOOL "Do not build tests")
#set(JANSSON_INSTALL_LIB_DIR ${CMAKE_BINARY_DIR}/local/lib CACHE PATH "Path to the local lib directory")
#
#FetchContent_Declare(
#        jansson
#        URL https://github.com/akheron/jansson/releases/download/v2.14/jansson-2.14.tar.gz
#        URL_HASH SHA256=5798d010e41cf8d76b66236cfb2f2543c8d082181d16bc3085ab49538d4b9929
#)
#FetchContent_MakeAvailable(jansson)

project(libjansson_builder C CXX)
include(ExternalProject)

set(JANSSON_INSTALL_LIB_DIR "lib/${BITS}")

#
# get jansson
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)


ExternalProject_Add(project_jansson
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        # GIT_REPOSITORY https://github.com/akheron/jansson.git
        URL http://www.digip.org/jansson/releases/jansson-2.13.1.tar.gz
        ${timestamp_policy}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        # DOWNLOAD_EXTRACT_TIMESTAMP OLD
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/jansson
        CMAKE_ARGS -DJANSSON_BUILD_DOCS=OFF -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
        -DJANSSON_INSTALL_LIB_DIR:String=${CMAKE_BINARY_DIR}/local/lib
)
ExternalProject_Get_Property(project_jansson install_dir)

add_library(jansson STATIC IMPORTED GLOBAL)
set_property(TARGET jansson PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libjansson${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(jansson project_jansson)
