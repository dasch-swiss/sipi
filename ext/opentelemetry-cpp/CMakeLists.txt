project(opentelemetry-cpp_builder C CXX)
include(ExternalProject)

set(OpenTelemetryVersion "v1.14.2")
ExternalProject_Add(opentelemetry-cpp
        DEPENDS abseil-cpp
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        GIT_REPOSITORY https://github.com/open-telemetry/opentelemetry-cpp
        GIT_TAG ${OpenTelemetryVersion}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/opentelemetry-cpp
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
        -DBUILD_SHARED_LIBS=FALSE
        -DWITH_ABSEIL=TRUE
        -DWITH_OTLP_HTTP=TRUE
        -DWITH_OTLP_GRPC=FALSE
        -DWITH_BUILD_TESTING=FALSE
        -DWITH_FUNC_TESTS=FALSE
        -DWITH_EXAMPLES=FALSE
        -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
        -DCMAKE_INSTALL_LIBDIR=${CMAKE_BINARY_DIR}/local/lib
)

ExternalProject_Get_Property(opentelemetry-cpp install_dir source_dir binary_dir)

message(STATUS "opentelemetry-cpp_SOURCE_DIR: ${source_dir}")
message(STATUS "opentelemetry-cpp_BINARY_DIR: ${binary_dir}")

# Setting the absl_DIR to make absl findable
set(opentelemetry_DIR ${source_dir} CACHE INTERNAL "")
