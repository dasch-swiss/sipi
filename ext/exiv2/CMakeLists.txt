project(exiv2_builder C CXX)
include(FetchContent)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared library")
set(EXIV2_BUILD_SAMPLES OFF CACHE BOOL "Build samples")
set(EXIV2_BUILD_EXIV2_COMMAND OFF CACHE BOOL "Build exiv2 command")
set(EXIV2_ENABLE_NLS OFF CACHE BOOL "Enable NLS")
set(EXIV2_ENABLE_BROTLI OFF CACHE BOOL "Enable brotli")
set(EXPAT_USE_STATIC_LIBS ON CACHE BOOL "Use static expat library")
set(EXPAT_LIBRARY expat CACHE FILEPATH "Path to expat library")
set(CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR}/local CACHE PATH "Path to the local directory")
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/local CACHE PATH "Path to the local directory")
set(CMAKE_INSTALL_LIBDIR ${CMAKE_BINARY_DIR}/local/lib CACHE PATH "Path to the local lib directory")

FetchContent_Declare(
        exiv2
        URL https://github.com/Exiv2/exiv2/archive/refs/tags/v0.28.2.tar.gz
        URL_HASH SHA256=543bead934135f20f438e0b6d8858c55c5fcb7ff80f5d1d55489965f1aad58b9
)
FetchContent_MakeAvailable(exiv2)

add_dependencies(exiv2lib expat png_static )
