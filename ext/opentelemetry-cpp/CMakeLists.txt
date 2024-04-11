project(opentelemetry_builder C CXX)
include(ExternalProject)

set(OpenTelemetryVersion "v1.14.2")

ExternalProject_Add(project_opentelemetry
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        GIT_REPOSITORY https://github.com/open-telemetry/opentelemetry-cpp
        GIT_TAG ${OpenTelemetryVersion}
        GIT_SUBMODULES_RECURSE TRUE
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/open_telemetry
        CMAKE_ARGS -DBUILD_SHARED_LIBS=OFF -DWITH_ABSEIL=ON -DWITH_OTLP_HTTP=ON -DWITH_OTLP_GRPC=OFF -DBUILD_TESTING=OFF -DWITH_FUNC_TESTS=OFF -DWITH_EXAMPLES=OFF -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
)

add_library(otel INTERFACE)
target_link_libraries(otel INTERFACE
        ${CMAKE_BINARY_DIR}/local/lib/libopentelemetry_common${CMAKE_STATIC_LIBRARY_SUFFIX}
        ${CMAKE_BINARY_DIR}/local/lib/libopentelemetry_exporter_ostream_span${CMAKE_STATIC_LIBRARY_SUFFIX}
        ${CMAKE_BINARY_DIR}/local/lib/libopentelemetry_exporter_otlp_http${CMAKE_STATIC_LIBRARY_SUFFIX}
        ${CMAKE_BINARY_DIR}/local/lib/libopentelemetry_exporter_otlp_http_client${CMAKE_STATIC_LIBRARY_SUFFIX}
        ${CMAKE_BINARY_DIR}/local/lib/libopentelemetry_logs${CMAKE_STATIC_LIBRARY_SUFFIX}
        ${CMAKE_BINARY_DIR}/local/lib/libopentelemetry_metrics${CMAKE_STATIC_LIBRARY_SUFFIX}
        ${CMAKE_BINARY_DIR}/local/lib/libopentelemetry_resources${CMAKE_STATIC_LIBRARY_SUFFIX}
        ${CMAKE_BINARY_DIR}/local/lib/libopentelemetry_trace${CMAKE_STATIC_LIBRARY_SUFFIX}

)
add_dependencies(otel project_opentelemetry)
