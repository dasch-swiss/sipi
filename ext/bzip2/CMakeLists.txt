project(bzip2_builder C CXX)
include(FetchContent)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
set(ENABLE_SHARED_LIB OFF CACHE BOOL "Build shared library")
set(ENABLE_STATIC_LIB ON CACHE BOOL "Build static library")

# FetchContent configuration for bzip2
FetchContent_Declare(
        bzip2
        GIT_REPOSITORY https://gitlab.com/bzip2/bzip2
        GIT_TAG 66c46b8c9436613fd81bc5d03f63a61933a4dcc3  # Specific commit from Jun 01, 2023
)
FetchContent_MakeAvailable(bzip2)
