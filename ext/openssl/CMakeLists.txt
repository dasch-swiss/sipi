cmake_minimum_required(VERSION 3.19)
project(ssl_builder C CXX)
include(ExternalProject)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    if (${CMAKE_HOST_SYSTEM_PROCESSOR} MATCHES "x86_64")
        set(CONF_ARCHOPT darwin64-x86_64-cc)
    else()
        set(CONF_ARCHOPT darwin64-arm64-cc)
    endif()
else()
    set(CONF_ARCHOPT "")
endif()

ExternalProject_Add(project_ssl
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://www.openssl.org/source/openssl-3.0.1.tar.gz
        SOURCE_DIR ${COMMON_SRCS}/openssl
        CONFIGURE_COMMAND ${COMMON_SRCS}/openssl/Configure ${CONF_ARCHOPT}
        --prefix=${COMMON_LOCAL}
        --libdir=${CONFIGURE_LIBDIR}
        BUILD_COMMAND make
        INSTALL_COMMAND make INSTALL_LIB=${CONFIGURE_LIBDIR} install INSTALL_TOP=${COMMON_LOCAL}
        BUILD_IN_SOURCE 1)

ExternalProject_Get_Property(project_ssl install_dir)

if(MAKE_SHARED_CSERVE)
    add_library(crypto SHARED IMPORTED GLOBAL)
    set_property(TARGET crypto PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libcrypto${CMAKE_SHARED_LIBRARY_SUFFIX})
    add_library(ssl SHARED IMPORTED GLOBAL)
    set_property(TARGET openssl PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libssl${CMAKE_SHARED_LIBRARY_SUFFIX})
else()
    add_library(crypto STATIC IMPORTED GLOBAL)
    set_target_properties(crypto PROPERTIES IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libcrypto${CMAKE_STATIC_LIBRARY_SUFFIX})
    add_library(ssl STATIC IMPORTED GLOBAL)
    set_target_properties(ssl PROPERTIES IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libssl${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()

add_dependencies(crypto project_ssl)
add_dependencies(ssl project_ssl)
