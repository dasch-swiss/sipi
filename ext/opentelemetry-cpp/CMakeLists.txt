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
        CMAKE_ARGS -DBUILD_SHARED_LIBS=OFF -DWITH_ABSEIL=ON -DWITH_OTLP_GRPC=ON -DWITH_PROMETHEUS=ON -DBUILD_TESTING=OFF -DWITH_EXAMPLES=OFF -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
)
ExternalProject_Get_Property(project_opentelemetry install_dir)

add_library(otel_common STATIC IMPORTED GLOBAL)
set_property(TARGET otel_common PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libopentelemetry_common${CMAKE_STATIC_LIBRARY_SUFFIX})
add_dependencies(otel_common project_opentelemetry)

add_library(otel_trace STATIC IMPORTED GLOBAL)
set_property(TARGET otel_trace PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libopentelemetry_trace${CMAKE_STATIC_LIBRARY_SUFFIX})
add_dependencies(otel_trace project_opentelemetry)

add_library(otel_exporter STATIC IMPORTED GLOBAL)
set_property(TARGET otel_exporter PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libopentelemetry_exporter_ostream_span${CMAKE_STATIC_LIBRARY_SUFFIX})
add_dependencies(otel_exporter project_opentelemetry)

add_library(otel_resources STATIC IMPORTED GLOBAL)
set_property(TARGET otel_resources PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libopentelemetry_resources${CMAKE_STATIC_LIBRARY_SUFFIX})
add_dependencies(otel_resources project_opentelemetry)
