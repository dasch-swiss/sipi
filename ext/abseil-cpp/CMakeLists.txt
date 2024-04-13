project(abseil_builder C CXX)
include(ExternalProject)

set(AbseilVersion "20240116.2")
ExternalProject_Add(abseil-cpp
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG ${AbseilVersion}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/abseil-cpp
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=FALSE -DABSL_ENABLE_INSTALL=TRUE -DBUILD_TESTING=FALSE -DABSL_BUILD_TESTING=FALSE -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local -DCMAKE_INSTALL_LIBDIR=${CMAKE_BINARY_DIR}/local/lib
)

ExternalProject_Get_Property(abseil-cpp install_dir source_dir binary_dir)

message(STATUS "abseil-cpp_SOURCE_DIR: ${source_dir}")
message(STATUS "abseil-cpp_BINARY_DIR: ${binary_dir}")

# Setting the absl_DIR to make absl findable
set(absl_DIR ${source_dir} CACHE INTERNAL "")
