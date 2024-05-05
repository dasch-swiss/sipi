project(expat_builder C CXX)
include(FetchContent)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared library")
set(EXPAT_BUILD_TOOLS OFF CACHE BOOL "Build tools")
set(EXPAT_BUILD_EXAMPLES OFF CACHE BOOL "Build examples")
set(EXPAT_BUILD_DOCS OFF CACHE BOOL "Enable documentation")
set(CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR}/local CACHE PATH "Path to the local directory")
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/local CACHE PATH "Path to the local directory")
set(CMAKE_INSTALL_LIBDIR ${CMAKE_BINARY_DIR}/local/lib CACHE PATH "Path to the local lib directory")

FetchContent_Declare(
        expat
        URL https://github.com/libexpat/libexpat/releases/download/R_2_5_0/expat-2.5.0.tar.bz2
        URL_HASH SHA256=6f0e6e01f7b30025fa05c85fdad1e5d0ec7fd35d9f61b22f34998de11969ff67
)
FetchContent_MakeAvailable(expat)
