#
# This file needs to be updated whenever a new test is added.
#
# all tests can be run from the build directory with `make test`
include(CheckCXXCompilerFlag)
#set(CMAKE_CXX_STANDARD 17)
check_cxx_compiler_flag("-fvisibility-inlines-hidden" SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG)
if(SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden")
endif()
check_cxx_compiler_flag("-fvisibility=hidden" SUPPORTS_FVISIBILITY_FLAG)
if(SUPPORTS_FVISIBILITY_FLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif()

find_package(Threads REQUIRED)

# Enable ExternalProject CMake module
include(ExternalProject)

# Download and install GoogleTest
ExternalProject_Add(
        gtest
        URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz
        #DOWNLOAD_EXTRACT_TIMESTAMP NEW
        #GIT_REPOSITORY https://github.com/google/googletest.git
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}/gtest
        # Disable install step
        INSTALL_COMMAND ""
)

# Get GTest source and binary directories from CMake project
ExternalProject_Get_Property(gtest source_dir binary_dir)

# Create a libgtest target to be used as a dependency by test programs
add_library(libgtest IMPORTED STATIC GLOBAL)
add_dependencies(libgtest gtest)

# Set libgtest properties
set_target_properties(libgtest PROPERTIES
        "IMPORTED_LOCATION" "${binary_dir}/lib/libgtest.a"
        "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
        )

# Create a libgmock target to be used as a dependency by test programs
add_library(libgmock IMPORTED STATIC GLOBAL)
add_dependencies(libgmock gtest)

# Set libgmock properties
set_target_properties(libgmock PROPERTIES
        "IMPORTED_LOCATION" "${binary_dir}/lib/libgmock.a"
        "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
        )

# I couldn't make it work with INTERFACE_INCLUDE_DIRECTORIES
include_directories("${source_dir}/googletest/include"
        "${source_dir}/googlemock/include")


# Configuration parsing and tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./configuration/configuration)'
add_subdirectory(configuration)
#add_test(NAME configuration_unit_test
#        WORKING_DIRECTORY
#        COMMAND configuration)

# SipiImage tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./sipiimage/sipiimage)'
add_subdirectory(sipiimage)
#add_test(NAME sipiimage_unit_test
#        WORKING_DIRECTORY
#        COMMAND sipiimage)

