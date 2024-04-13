project(googletest_builder C CXX)
include(ExternalProject)

set(GoogleTestVersion "main")
ExternalProject_Add(project_googletest
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        GIT_REPOSITORY https://github.com/google/googletest
        GIT_TAG ${GoogleTestVersion}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/googletest
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=FALSE -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local -DCMAKE_INSTALL_LIBDIR=${CMAKE_BINARY_DIR}/local/lib
)

ExternalProject_Get_Property(project_googletest install_dir source_dir binary_dir)

# order matters
# here we only add the googletest libraries which are used in the project
add_library(googletest INTERFACE)
target_include_directories(googletest INTERFACE "${install_dir}/include/gtest" "${install_dir}/include/gmock")
target_link_libraries(googletest INTERFACE "${install_dir}/lib/libgtest.a" "${install_dir}/lib/libgtest_main.a" "${install_dir}/lib/libgmock.a" "${install_dir}/lib/libgmock_main.a")
