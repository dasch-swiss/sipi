include(FetchContent)

set(OpenTelemetryVersion "v1.14.2")

FetchContent_Declare(
        opentelemetry-cpp
        GIT_REPOSITORY https://github.com/open-telemetry/opentelemetry-cpp
        GIT_TAG ${OpenTelemetryVersion}
)

# Manually populate and check the properties
FetchContent_GetProperties(opentelemetry-cpp)
if (NOT opentelemetry-cpp_POPULATED)
    FetchContent_Populate(opentelemetry-cpp)
    execute_process(COMMAND git submodule update --init --recursive WORKING_DIRECTORY ${opentelemetry-cpp_SOURCE_DIR})

    # Set flags specific for the configuration of the fetched content
    set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ standard to conform to" FORCE)
    set(BUILD_SHARED_LIBS FALSE CACHE BOOL "Build shared libraries (DLLs)." FORCE)
    set(WITH_ABSEIL TRUE CACHE BOOL "Use Abseil" FORCE)
    set(WITH_OTLP_HTTP TRUE CACHE BOOL "Use HTTP" FORCE)
    set(WITH_OTLP_GRPC FALSE CACHE BOOL "Use gRPC" FORCE)
    set(BUILD_TESTING FALSE CACHE BOOL "Build tests" FORCE)
    set(WITH_FUNC_TESTS FALSE CACHE BOOL "Build functional tests" FORCE)
    set(WITH_EXAMPLES FALSE CACHE BOOL "Build examples" FORCE)

    # Add the fetched content's source directory to the build (triggers the build)
    add_subdirectory(${opentelemetry-cpp_SOURCE_DIR} ${opentelemetry-cpp_BINARY_DIR})
endif ()

# order matters
# here we only add the opentelemetry libraries which are used in the project
add_library(otel INTERFACE)
target_include_directories(otel INTERFACE "${opentelemetry-cpp_SOURCE_DIR}/api/include")
target_link_libraries(otel INTERFACE
        opentelemetry_trace
        opentelemetry_resources
        opentelemetry_common
        opentelemetry_exporter_ostream_logs
        opentelemetry_exporter_otlp_http
        opentelemetry_exporter_otlp_http_metric
        opentelemetry_exporter_otlp_http_log
)
