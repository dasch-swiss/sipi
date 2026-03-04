project(libluarocks_builder C CXX)
include(ExternalProject)

#
# get luarocks
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

# When building static binaries with Zig, the cross-compiled (or musl-targeted)
# lua interpreter may not be executable on the build host. Use a host-native
# lua for luarocks bootstrapping while still linking against the built liblua.a.
if(ZIG_STATIC_BUILD)
    find_program(HOST_LUA_EXECUTABLE NAMES lua lua5.3 lua-5.3)
    if(HOST_LUA_EXECUTABLE)
        get_filename_component(_luarocks_lua_bin_dir "${HOST_LUA_EXECUTABLE}" DIRECTORY)
        message(STATUS "Luarocks: using host lua for bootstrap: ${HOST_LUA_EXECUTABLE}")
    else()
        set(_luarocks_lua_bin_dir "${CMAKE_BINARY_DIR}/local/bin")
        message(WARNING "Luarocks: no host lua found, using built lua (may fail for cross-builds)")
    endif()
else()
    set(_luarocks_lua_bin_dir "${CMAKE_BINARY_DIR}/local/bin")
endif()

ExternalProject_Add(project_luarocks
        DEPENDS lua
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL http://luarocks.github.io/luarocks/releases/luarocks-3.3.1.tar.gz
        ${timestamp_policy}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        #DOWNLOAD_EXTRACT_TIMESTAMP NEW
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/luarocks-3.3.1
        CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/luarocks-3.3.1/configure
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
