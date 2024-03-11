project(curl_builder C CXX)
include(ExternalProject)

#
# get libcurl
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

ExternalProject_Add(project_curl
        DEPENDS ssl
        DEPENDS crypto
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://curl.haxx.se/download/curl-8.4.0.tar.gz
        ${timestamp_policy}
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        SOURCE_DIR ${COMMON_SRCS}/curl
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
