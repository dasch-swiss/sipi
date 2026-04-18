project(curl_builder C CXX)
include(ExternalProject)

#
# libcurl - built from source as a static library
#
# Depends on: OpenSSL (built from source), zlib
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

set(CURL_EXTRA_CONFIGURE_FLAGS "")
if(APPLE AND ZIG_TOOLCHAIN)
    # Avoid CoreFoundation/SystemConfiguration linkage from curl's macOS IPv6 proxy bootstrap.
    list(APPEND CURL_EXTRA_CONFIGURE_FLAGS --disable-ipv6)
endif()

# Zig musl static builds: configure needs -target in CFLAGS and -static in
# LDFLAGS. musl bundles pthread/dl into libc so we set LIBS="" to prevent
# configure from searching for separate -ldl/-lpthread.
set(_curl_cflags "${CMAKE_C_FLAGS} -I${CMAKE_BINARY_DIR}/local/include")
set(_curl_ldflags "-L${CMAKE_BINARY_DIR}/local/lib")
set(_curl_libs "")
if(DEFINED ZIG_STATIC_BUILD AND ZIG_STATIC_BUILD)
    set(_curl_cflags "-target ${ZIG_TARGET} -O2 -I${CMAKE_BINARY_DIR}/local/include")
    set(_curl_ldflags "-L${CMAKE_BINARY_DIR}/local/lib -static")
    set(_curl_libs "LIBS=")
endif()

# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_CURL_FILENAME}")
if(EXISTS "${_local}")
    set(_curl_url "${_local}")
else()
    set(_curl_url "${DEP_CURL_URL}")
endif()

ExternalProject_Add(project_curl
    INSTALL_DIR ${CMAKE_BINARY_DIR}/local
    URL "${_curl_url}"
    URL_HASH SHA256=${DEP_CURL_SHA256}
    DOWNLOAD_DIR ""
    ${timestamp_policy}
    SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/curl
    CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/curl/configure
        --prefix=${CMAKE_BINARY_DIR}/local
        --libdir=${CMAKE_BINARY_DIR}/local/lib
        --disable-shared
        --enable-static
        --with-openssl=${CMAKE_BINARY_DIR}/local
        --with-zlib=${CMAKE_BINARY_DIR}/local
        --without-libpsl
        --without-brotli
        --without-zstd
        --without-libidn2
        --without-nghttp2
        --disable-ldap
        --disable-ldaps
        --disable-rtsp
        --disable-dict
        --disable-telnet
        --disable-tftp
        --disable-pop3
        --disable-imap
        --disable-smb
        --disable-smtp
        --disable-gopher
        --disable-mqtt
        --disable-manual
        # Skip all man-page generation. The doc scripts use `#!/usr/bin/env bash`
        # shebangs which fail in the Nix build sandbox (no /usr/bin/env).
        --disable-docs
        ${CURL_EXTRA_CONFIGURE_FLAGS}
        ${ZIG_AUTOTOOLS_HOST}
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        ${ZIG_EP_CPP}
        "CFLAGS=${_curl_cflags}"
        "LDFLAGS=${_curl_ldflags}"
        ${_curl_libs}
    BUILD_COMMAND make -j4
    BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_curl install_dir)

add_library(curl_static STATIC IMPORTED GLOBAL)
set_property(TARGET curl_static PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libcurl${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(project_curl project_openssl project_zlib)
add_dependencies(curl_static project_curl)
