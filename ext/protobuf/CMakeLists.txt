project(googletest_builder C CXX)
include(ExternalProject)

set(ProtobufVersion "v26.0")
ExternalProject_Add(protobuf
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        GIT_REPOSITORY https://github.com/protocolbuffers/protobuf
        GIT_TAG ${ProtobufVersion}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/protobuf
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
        -DBUILD_SHARED_LIBS=FALSE
        -Dprotobuf_BUILD_TESTS=FALSE
        -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
        -DCMAKE_INSTALL_LIBDIR=${CMAKE_BINARY_DIR}/local/lib
)

ExternalProject_Get_Property(protobuf install_dir source_dir binary_dir)

message(STATUS "protobuf_SOURCE_DIR: ${source_dir}")
message(STATUS "protobuf_BINARY_DIR: ${binary_dir}")

# Setting the absl_DIR to make absl findable
set(protobuf_DIR ${source_dir} CACHE INTERNAL "")
