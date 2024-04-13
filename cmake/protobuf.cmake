include(FetchContent)

set(ProtobufVersion "v26.0")
FetchContent_Declare(
        protobuf
        GIT_REPOSITORY https://github.com/protocolbuffers/protobuf
        GIT_TAG ${ProtobufVersion}
)

# Manually populate and check the properties
FetchContent_GetProperties(protobuf)
if (NOT protobuf_POPULATED)
    FetchContent_Populate(protobuf)

    # Set flags specific for the configuration of the fetched content
    set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ standard to conform to" FORCE)
    set(BUILD_SHARED_LIBS FALSE CACHE BOOL "Build shared libraries (DLLs)." FORCE)
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "Build tests" FORCE)

    # Add the fetched content's source directory to the build (triggers the build)
    add_subdirectory(${protobuf_SOURCE_DIR} ${protobuf_BINARY_DIR})
endif ()
