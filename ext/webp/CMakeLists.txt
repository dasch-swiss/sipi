project(webp_builder C CXX)
include(FetchContent)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared library")
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "Build anim utils")
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "Build vwebp")
set(CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR}/local CACHE PATH "Path to the local directory")
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/local CACHE PATH "Path to the local directory")
set(CMAKE_INSTALL_LIBDIR ${CMAKE_BINARY_DIR}/local/lib CACHE PATH "Path to the local lib directory")

FetchContent_Declare(
        webp
        URL https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.1.0.tar.gz
        URL_HASH SHA256=98a052268cc4d5ece27f76572a7f50293f439c17a98e67c4ea0c7ed6f50ef043
)
FetchContent_MakeAvailable(webp)
