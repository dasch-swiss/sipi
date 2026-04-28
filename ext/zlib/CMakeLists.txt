project(zlib_builder C CXX)
include(ExternalProject)

#
# zlib
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_ZLIB_FILENAME}")
if(EXISTS "${_local}")
    set(_zlib_url "${_local}")
else()
    set(_zlib_url "${DEP_ZLIB_URL}")
endif()

ExternalProject_Add(project_zlib
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_zlib_url}"
        URL_HASH SHA256=${DEP_ZLIB_SHA256}
        DOWNLOAD_DIR ""
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/zlib
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        CC=${CMAKE_C_COMPILER}
        ${CMAKE_BINARY_DIR}/local/extsrcs/zlib/configure
        --prefix=${CMAKE_BINARY_DIR}/local
        --static
        # --shared
        --libdir=${CMAKE_BINARY_DIR}/local/lib
        BUILD_COMMAND make
        INSTALL_COMMAND make INSTALL_LIB=${CMAKE_BINARY_DIR}/local/lib install INSTALL_TOP=${CMAKE_BINARY_DIR}/local
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_zlib install_dir)

add_library(zlib STATIC IMPORTED GLOBAL)
set_property(TARGET zlib PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libz${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(zlib project_zlib)
