project(libluarocks_builder C CXX)
include(ExternalProject)

#
# get luarocks
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

set(_luarocks_lua_bin_dir "${CMAKE_BINARY_DIR}/local/bin")

# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_LUAROCKS_FILENAME}")
if(EXISTS "${_local}")
    set(_luarocks_url "${_local}")
else()
    set(_luarocks_url "${DEP_LUAROCKS_URL}")
endif()

ExternalProject_Add(project_luarocks
        DEPENDS lua
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_luarocks_url}"
        URL_HASH SHA256=${DEP_LUAROCKS_SHA256}
        ${timestamp_policy}
        DOWNLOAD_DIR ""
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/luarocks-${DEP_LUAROCKS_VERSION}
        CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/luarocks-${DEP_LUAROCKS_VERSION}/configure
        --prefix=${CMAKE_BINARY_DIR}/local
        --with-lua-bin=${_luarocks_lua_bin_dir}
        --with-lua-include=${CMAKE_BINARY_DIR}/local/include
        --with-lua-lib=${CMAKE_BINARY_DIR}/local/lib
        --rocks-tree=${CMAKE_INSTALL_PREFIX}/luarocks
        #    --libdir=${CMAKE_BINARY_DIR}/local/lib
        BUILD_COMMAND make build
        INSTALL_COMMAND make install
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_luarocks install_dir)

add_library(luarocks STATIC IMPORTED GLOBAL)

add_dependencies(luarocks project_luarocks)
