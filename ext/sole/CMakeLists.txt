project(sole_builder C CXX)
include(FetchContent)

FetchContent_Declare(
        sole
        GIT_REPOSITORY https://github.com/r-lyeh-archived/sole
        GIT_TAG 95612e5cda1accc0369a51edfe0f32bfb4bee2a0 # You can specify a particular tag or commit if required
)

# Manually populate and check the properties
FetchContent_GetProperties(sole)
if (NOT sole_POPULATED)
    FetchContent_Populate(sole)
    # Add the fetched content's source directory to the build (triggers the build)
endif ()

# message(STATUS "sole source dir: ${sole_SOURCE_DIR}")
