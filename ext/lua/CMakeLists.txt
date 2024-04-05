project(liblua_builder C CXX)
include(ExternalProject)


#
# get lua
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

ExternalProject_Add(project_lua
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL http://www.lua.org/ftp/lua-5.3.5.tar.gz
        ${timestamp_policy}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        #DOWNLOAD_EXTRACT_TIMESTAMP NEW
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/lua-5.3.5
        CONFIGURE_COMMAND ls
        #    BUILD_COMMAND make CPPFLAGS=-DLUA_USE_APICHECK CFLAGS=-DLUA_USE_APICHECK INSTALL_LIB=${CMAKE_BINARY_DIR}/local/lib ${LUA_ARCH}
        BUILD_COMMAND make INSTALL_LIB=${CMAKE_BINARY_DIR}/local/lib ${LUA_ARCH}
        INSTALL_COMMAND make INSTALL_LIB=${CMAKE_BINARY_DIR}/local/lib install INSTALL_TOP=${CMAKE_BINARY_DIR}/local
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_lua install_dir)

add_library(lua STATIC IMPORTED GLOBAL)
set_property(TARGET lua PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/liblua${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(lua project_lua)
