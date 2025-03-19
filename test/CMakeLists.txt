#
# This file needs to be updated whenever a new test is added.
#
# all tests can be run from the build directory with `make test`

# Enable ExternalProject CMake module
include(ExternalProject)

# Download and install GoogleTest
ExternalProject_Add(
        gtest
        URL https://github.com/google/googletest/archive/refs/heads/main.zip
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
include_directories("${source_dir}/googletest/include" "${source_dir}/googlemock/include")

# Download and install ApprovalTests.cpp single header distribution
set(HEADER_URL "https://github.com/approvals/ApprovalTests.cpp/releases/download/v.10.13.0/ApprovalTests.v.10.13.0.hpp")
set(HEADER_FILE "${CMAKE_BINARY_DIR}/local/include/ApprovalTests.hpp")

if(NOT EXISTS "${HEADER_FILE}")
        file(DOWNLOAD ${HEADER_URL} ${HEADER_FILE}
                TIMEOUT 60  # Set a timeout for the download in seconds
                EXPECTED_HASH SHA256=c00f6390b81d9924dc646e9d32b61e1e09abda106c13704f714ac349241bb9ff # Optional: Verify file integrity
                TLS_VERIFY ON)  # Verify TLS/SSL certificates; ON by default
endif()

include_directories("${CMAKE_BINARY_DIR}/local/include")

if(NOT TARGET ApprovalTests)
        set(LIB_NAME ApprovalTests)
        add_library(${LIB_NAME} INTERFACE)
        target_include_directories(${LIB_NAME} INTERFACE "${COMON_LOCAL_INCLUDE}")
        add_library(${LIB_NAME}::${LIB_NAME} ALIAS ${LIB_NAME})
endif()


# Approval tests
add_subdirectory(approval)

# Configuration parsing and tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./configuration/configuration)'
add_subdirectory(unit/configuration)

# SipiImage tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./sipiimage/sipiimage)'
add_subdirectory(unit/sipiimage)

# SipiImage tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./sipiimage/sipiimage)'
add_subdirectory(unit/logger)

# Handlers tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./handlers/handlers)'
add_subdirectory(unit/handlers)
