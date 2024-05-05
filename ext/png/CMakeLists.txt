project(png_builder C CXX)
include(FetchContent)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
set(PNG_SHARED OFF CACHE BOOL "Build shared library")
set(PNG_STATIC ON CACHE BOOL "Build static library")
set(PNG_FRAMEWORK OFF CACHE BOOL "Build framework")
set(PNG_TESTS ON CACHE BOOL "Enable tests")
set(CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR}/local CACHE PATH "Path to the local directory")
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/local CACHE PATH "Path to the local directory")
set(CMAKE_INSTALL_LIBDIR ${CMAKE_BINARY_DIR}/local/lib CACHE PATH "Path to the local lib directory")
set(PNG_ARM_NEON on CACHE STRING "Enable ARM NEON support")
set(PNG_TESTS OFF CACHE BOOL "Enable tests")
set(PNG_BUILD_ZLIB OFF CACHE BOOL "Build zlib")
set(ZLIB_ROOT ON CACHE BOOL "Use ZLIB_ROOT")
set(ZLIB_INCLUDE_DIR ${CMAKE_BINARY_DIR}/local/include CACHE PATH "Path to the local include directory")
set(ZLIB_LIBRARY ${CMAKE_BINARY_DIR}/local/lib/libz${CMAKE_STATIC_LIBRARY_SUFFIX} CACHE FILEPATH "Path to the local zlib library")

FetchContent_Declare(
        png
        URL https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.43.tar.gz
        URL_HASH SHA256=fecc95b46cf05e8e3fc8a414750e0ba5aad00d89e9fdf175e94ff041caf1a03a
)
FetchContent_MakeAvailable(png)
