#
# This file needs to be updated whenever a new test is added.
#
# all tests can be run from the build directory with `make test`

# Enable ExternalProject CMake module
include(ExternalProject)

# Download and install GoogleTest
ExternalProject_Add(
        gtest
        URL https://github.com/google/googletest/archive/refs/tags/v1.16.0.zip
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}/gtest
        CMAKE_ARGS ${ZIG_EP_CMAKE_ARGS}
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


# ---------------------------------------------------------------------------
# Shared testable library: compile all production sources once, link per test
# ---------------------------------------------------------------------------
# Excludes src/sipi.cpp (has main()) and shttps/Shttp.cpp (manual test program)
set(SIPI_TESTABLE_SRCS
    ${CMAKE_SOURCE_DIR}/src/SipiConf.cpp
    ${CMAKE_SOURCE_DIR}/src/SipiRateLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/SipiError.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/SipiIcc.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/SipiXmp.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/SipiIptc.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/SipiExif.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/SipiEssentials.cpp
    ${CMAKE_SOURCE_DIR}/src/SipiImage.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/SipiIOTiff.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/SipiIOJ2k.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/SipiIOJpeg.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/SipiIOPng.cpp
    ${CMAKE_SOURCE_DIR}/src/SipiHttpServer.cpp
    ${CMAKE_SOURCE_DIR}/src/handlers/iiif_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/SipiCache.cpp
    ${CMAKE_SOURCE_DIR}/src/SipiMetrics.cpp
    ${CMAKE_SOURCE_DIR}/src/SipiLua.cpp
    ${CMAKE_SOURCE_DIR}/src/iiifparser/SipiIdentifier.cpp
    ${CMAKE_SOURCE_DIR}/src/iiifparser/SipiRotation.cpp
    ${CMAKE_SOURCE_DIR}/src/iiifparser/SipiQualityFormat.cpp
    ${CMAKE_SOURCE_DIR}/src/iiifparser/SipiRegion.cpp
    ${CMAKE_SOURCE_DIR}/src/iiifparser/SipiSize.cpp
    ${CMAKE_SOURCE_DIR}/src/SipiCommon.cpp
    ${CMAKE_SOURCE_DIR}/src/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/SipiFilenameHash.cpp
    ${CMAKE_SOURCE_DIR}/shttps/Error.cpp
    ${CMAKE_SOURCE_DIR}/shttps/Hash.cpp
    ${CMAKE_SOURCE_DIR}/shttps/SockStream.cpp
    ${CMAKE_SOURCE_DIR}/shttps/ChunkReader.cpp
    ${CMAKE_SOURCE_DIR}/shttps/Connection.cpp
    ${CMAKE_SOURCE_DIR}/shttps/LuaServer.cpp
    ${CMAKE_SOURCE_DIR}/shttps/LuaSqlite.cpp
    ${CMAKE_SOURCE_DIR}/shttps/Parsing.cpp
    ${CMAKE_SOURCE_DIR}/shttps/ThreadControl.cpp
    ${CMAKE_SOURCE_DIR}/shttps/SocketControl.cpp
    ${CMAKE_SOURCE_DIR}/shttps/Server.cpp
    ${CMAKE_SOURCE_DIR}/shttps/jwt.c
)

add_library(libsipi_testable STATIC ${SIPI_TESTABLE_SRCS})

target_include_directories(libsipi_testable PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/iiifparser
    ${CMAKE_SOURCE_DIR}/include/metadata
    ${CMAKE_SOURCE_DIR}/include/formats
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/shttps
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/local/include
    ${CMAKE_BINARY_DIR}/generated
)

add_dependencies(libsipi_testable
    icc_profiles magic_database
    bzip2 zlib xz zstd jpeg jbigkit png webp tiff expat
    lcms2 exiv2 jansson lua sqlite3 kdu kdu_aux
    sentry prometheus_core openssl_ssl openssl_crypto curl_static magic_static
)

target_link_libraries(libsipi_testable PUBLIC
    lcms2 exiv2 expat tiff webp jbigkit png kdu_aux kdu xz
    magic_static lua jansson sqlite3 sentry prometheus_core
    ${CURL_LIBRARIES} ${OPENSSL_LIBRARIES}
    jpeg bzip2 zstd z m lzma
    ${SIPI_RUNTIME_SYSTEM_LIBS}
    build_config
)

# macOS needs CoreFoundation/SystemConfiguration (for libcurl) and iconv (for exiv2)
if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(libsipi_testable PUBLIC
        "-framework CoreFoundation"
        "-framework SystemConfiguration"
        iconv
    )
endif()

if (CODE_COVERAGE)
    target_link_libraries(libsipi_testable PUBLIC coverage_config)
endif()
if (ENABLE_SANITIZERS)
    target_link_libraries(libsipi_testable PUBLIC sanitizer_config)
endif()

# Approval tests
add_subdirectory(approval)

# Configuration parsing and tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./configuration/configuration)'
add_subdirectory(unit/configuration)

add_subdirectory(unit/iiifparser)

# Cache tests
add_subdirectory(unit/cache)

# SipiImage tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./sipiimage/sipiimage)'
add_subdirectory(unit/sipiimage)

# Logger tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./sipiimage/logger)'
add_subdirectory(unit/logger)

# Handlers tests
# To only run this single test, run from inside the build directory '(cd test/unit && ./handlers/handlers)'
add_subdirectory(unit/handlers)

# Filename hash tests
add_subdirectory(unit/filenamehash)

# SHTTPS utility tests (Hash, Parsing)
add_subdirectory(unit/shttps)
