project(openssl_builder C CXX)
include(ExternalProject)

#
# OpenSSL - built from source as a static library
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

# Map CMake system name + processor to OpenSSL target
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        set(OPENSSL_TARGET "linux-x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(OPENSSL_TARGET "linux-aarch64")
    else()
        message(FATAL_ERROR "Unsupported Linux architecture for OpenSSL: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(OPENSSL_TARGET "darwin64-arm64-cc")
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        set(OPENSSL_TARGET "darwin64-x86_64-cc")
    else()
        message(FATAL_ERROR "Unsupported macOS architecture for OpenSSL: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
else()
    message(FATAL_ERROR "Unsupported OS for OpenSSL: ${CMAKE_SYSTEM_NAME}")
endif()

# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_OPENSSL_FILENAME}")
if(EXISTS "${_local}")
    set(_openssl_url "${_local}")
else()
    set(_openssl_url "${DEP_OPENSSL_URL}")
endif()

ExternalProject_Add(project_openssl
    INSTALL_DIR ${CMAKE_BINARY_DIR}/local
    URL "${_openssl_url}"
    URL_HASH SHA256=${DEP_OPENSSL_SHA256}
    DOWNLOAD_DIR ""
    ${timestamp_policy}
    SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/openssl
    CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/openssl/Configure
        ${OPENSSL_TARGET}
        no-shared
        no-module
        no-pinshared
        no-tests
        --prefix=${CMAKE_BINARY_DIR}/local
        --libdir=lib
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        ${ZIG_EP_CFLAGS}
        ${ZIG_EP_CXXFLAGS}
    BUILD_COMMAND make -j4
    INSTALL_COMMAND make install_sw
    BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_openssl install_dir)

add_library(openssl_ssl STATIC IMPORTED GLOBAL)
set_property(TARGET openssl_ssl PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libssl${CMAKE_STATIC_LIBRARY_SUFFIX})

add_library(openssl_crypto STATIC IMPORTED GLOBAL)
set_property(TARGET openssl_crypto PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libcrypto${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(openssl_ssl project_openssl)
add_dependencies(openssl_crypto project_openssl)
