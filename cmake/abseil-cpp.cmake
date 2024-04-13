include(FetchContent)

FetchContent_Declare(
        abseil-cpp
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG 20240116.2
)

# Manually populate and check the properties
FetchContent_GetProperties(abseil-cpp)
if (NOT abseil-cpp_POPULATED)
    FetchContent_Populate(abseil-cpp)

    # Set flags specific for the configuration of the fetched content
    # Abseil currently supports C++14, C++17, and C++20.
    set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard to conform to" FORCE)
    set(CMAKE_CXX_STANDARD_REQUIRED TRUE CACHE BOOL "The compiler must support C++ standard ${CMAKE_CXX_STANDARD}" FORCE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "Propagate CXX standard to Abseil targets" FORCE)
    set(BUILD_SHARED_LIBS FALSE CACHE BOOL "Build shared libraries (DLLs)." FORCE)
    set(ABSL_ENABLE_INSTALL TRUE CACHE BOOL "Generate installation target" FORCE)
    set(BUILD_TESTING FALSE CACHE BOOL "Build tests" FORCE)
    set(ABSL_BUILD_TESTING FALSE CACHE BOOL "Build Abseil tests" FORCE)

    # Add the fetched content's source directory to the build
    add_subdirectory(${abseil-cpp_SOURCE_DIR} ${abseil-cpp_BINARY_DIR})
endif ()

message(STATUS "abseil-cpp_SOURCE_DIR: ${abseil-cpp_SOURCE_DIR}")
message(STATUS "abseil-cpp_BINARY_DIR: ${abseil-cpp_BINARY_DIR}")

# Setting the absl_DIR to make absl findable
set(absl_DIR ${abseil-cpp_BINARY_DIR} CACHE INTERNAL "")
