#
# This file needs to be updated whenever a new test is added.
#
# all tests can be run from the build directory with `make test`



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
        add_dependencies(${LIB_NAME} libgtest libgmock)
endif()


# Approval tests
add_subdirectory(approval)

# Configuration parsing and tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./configuration/configuration)'
add_subdirectory(unit/configuration)

# SipiImage tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./sipiimage/sipiimage)'
add_subdirectory(unit/sipiimage)

# Handlers tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./handlers/handlers)'
add_subdirectory(unit/handlers)
