cmake_minimum_required(VERSION 3.14.5)
project(curl_builder C CXX)
include(ExternalProject)

#
# Setting up openssl - things...
#
#if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
#    if (${CMAKE_HOST_SYSTEM_PROCESSOR} MATCHES "x86_64")
#        set(OPENSSL_ROOT_DIR /usr/local/opt/openssl)
#        set(OS_X_SPECIAL_FLAG -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl)
#    else()
#        set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl)
#        set(OS_X_SPECIAL_FLAG -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl)
#    endif()
#else()
#    set(OS_X_SPECIAL_FLAG "")
#endif()
#
#set(OPENSSL_ROOT_DIR ${COMMON_LOCAL})
#
#if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
#    set(OS_X_SPECIAL_FLAG -DOPENSSL_ROOT_DIR=${COMMON_SRCS}/openssl)
#endif()
#include(FindOpenSSL REQUIRED)
#
#message('==================== ${OPENSSL_FOUND}')

#
# get libcurl
#
ExternalProject_Add(project_curl
        DEPENDS ssl
        DEPENDS crypto
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://curl.haxx.se/download/curl-7.80.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP NEW
        SOURCE_DIR ${COMMON_SRCS}/curl-7.80.0
        CMAKE_ARGS  -DCMAKE_PREFIX_PATH=${COMMON_LOCAL}
        -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL}
        -DBUILD_SHARED_LIBS:BOOL=OFF
        -DCMAKE_USE_LIBSSH2:BOOL=OFF
        -DHTTP_ONLY:BOOL=ON
        ${OS_X_SPECIAL_FLAG}
        )

ExternalProject_Get_Property(project_curl install_dir)
if(MAKE_SHARED_CSERVE)
    add_library(curl SHARED IMPORTED GLOBAL)
    set_property(TARGET curl PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libcurl${CMAKE_SHARED_LIBRARY_SUFFIX})
    set(CMAKE_CREATE_SHARED "-DBUILD_SHARED_LIBS=ON")
else()
    add_library(curl STATIC IMPORTED GLOBAL)
    set_property(TARGET curl PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libcurl${CMAKE_STATIC_LIBRARY_SUFFIX})
    set(CMAKE_CREATE_SHARED "-DBUILD_SHARED_LIBS=OFF")
endif()
add_dependencies(curl project_curl)
