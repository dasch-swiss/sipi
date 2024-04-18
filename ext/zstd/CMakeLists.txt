project(zstd_builder C CXX)
include(FetchContent)

set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_SHARED OFF)

FetchContent_Declare(
        zstd
        URL https://github.com/facebook/zstd/releases/download/v1.5.6/zstd-1.5.6.tar.gz
        URL_HASH SHA256=8c29e06cf42aacc1eafc4077ae2ec6c6fcb96a626157e0593d5e82a34fd403c1
        SOURCE_SUBDIR build/cmake
)

FetchContent_MakeAvailable(zstd)

# Define the zstd library as an IMPORTED target
#add_library(zstd STATIC IMPORTED GLOBAL)
#set_target_properties(zstd PROPERTIES
#        IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libzstd${CMAKE_STATIC_LIBRARY_SUFFIX}
#        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/local/include
#)

add_dependencies(libzstd_static xz)
