include(FetchContent)

FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest
        GIT_TAG main
)

# Manually populate and check the properties
FetchContent_GetProperties(googletest)
if (NOT googletest_POPULATED)
    FetchContent_Populate(googletest)

    # Set flags specific for the configuration of the fetched content
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ standard to conform to" FORCE)
    set(BUILD_SHARED_LIBS FALSE CACHE BOOL "Build shared libraries (DLLs)." FORCE)

    # Add the fetched content's source directory to the build (triggers the build)
    add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})
endif ()

# message(STATUS "googletest_SOURCE_DIR: ${googletest_SOURCE_DIR}")
# message(STATUS "googletest_BINARY_DIR: ${googletest_BINARY_DIR}")
