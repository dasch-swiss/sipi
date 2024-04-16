project(liblua_builder C CXX)
include(ExternalProject)


#
# get lua
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

set(LuaVersion "5.4.6")
ExternalProject_Add(project_lua
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL http://www.lua.org/ftp/lua-${LuaVersion}.tar.gz
        URL_HASH SHA256=7d5ea1b9cb6aa0b59ca3dde1c6adcb57ef83a1ba8e5432c0ecd06bf439b3ad88
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/lua-${LuaVersion}
        CONFIGURE_COMMAND ""
        # BUILD_COMMAND make CPPFLAGS=-DLUA_USE_APICHECK CFLAGS=-DLUA_USE_APICHECK INSTALL_LIB=${CMAKE_BINARY_DIR}/local/lib ${LUA_ARCH}
        BUILD_COMMAND make CC=clang INSTALL_TOP=${CMAKE_BINARY_DIR}/local ${LUA_ARCH} test
        INSTALL_COMMAND make install INSTALL_TOP=${CMAKE_BINARY_DIR}/local
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_lua install_dir)

add_library(lua STATIC IMPORTED GLOBAL)
set_property(TARGET lua PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/liblua${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(lua project_lua)
