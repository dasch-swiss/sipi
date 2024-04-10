project(opentelemetry_builder C CXX)
include(ExternalProject)


# Download and install GoogleTest from main branch
ExternalProject_Add(project_gtest
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        GIT_REPOSITORY https://github.com/google/googletest
        GIT_TAG main
        GIT_SUBMODULES_RECURSE TRUE
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/googletest
        PREFIX ${CMAKE_BINARY_DIR}/local
        # Disable install step
        CMAKE_ARGS -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
)

# Get GTest source and binary directories from CMake project
ExternalProject_Get_Property(project_gtest source_dir binary_dir)

add_library(libgtest IMPORTED STATIC GLOBAL)
set_target_properties(libgtest PROPERTIES
        "IMPORTED_LOCATION" ${CMAKE_BINARY_DIR}/local/lib/libgtest${CMAKE_STATIC_LIBRARY_SUFFIX}
        "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
)
add_dependencies(libgtest project_gtest)

add_library(libgmock IMPORTED STATIC GLOBAL)
set_target_properties(libgmock PROPERTIES
        "IMPORTED_LOCATION" ${CMAKE_BINARY_DIR}/local/lib/libgmock${CMAKE_STATIC_LIBRARY_SUFFIX}
        "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
)
add_dependencies(libgmock project_gtest)

# I couldn't make it work with INTERFACE_INCLUDE_DIRECTORIES
include_directories("${CMAKE_BINARY_DIR}/local/include/gtest" "${CMAKE_BINARY_DIR}/local/include/gmock")
