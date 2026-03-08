project(liblua_builder C CXX)
include(ExternalProject)


#
# get lua
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_LUA_FILENAME}")
if(EXISTS "${_local}")
    set(_lua_url "${_local}")
else()
    set(_lua_url "${DEP_LUA_URL}")
endif()

ExternalProject_Add(project_lua
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_lua_url}"
        URL_HASH SHA256=${DEP_LUA_SHA256}
        ${timestamp_policy}
        DOWNLOAD_DIR ""
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/lua-${DEP_LUA_VERSION}
        CONFIGURE_COMMAND ls
        #    BUILD_COMMAND make CPPFLAGS=-DLUA_USE_APICHECK CFLAGS=-DLUA_USE_APICHECK INSTALL_LIB=${CMAKE_BINARY_DIR}/local/lib ${LUA_ARCH}
        BUILD_COMMAND make CC=${CMAKE_C_COMPILER} "MYCFLAGS=${CMAKE_C_FLAGS}" INSTALL_LIB=${CMAKE_BINARY_DIR}/local/lib ${LUA_ARCH}
        INSTALL_COMMAND make CC=${CMAKE_C_COMPILER} "MYCFLAGS=${CMAKE_C_FLAGS}" INSTALL_LIB=${CMAKE_BINARY_DIR}/local/lib install INSTALL_TOP=${CMAKE_BINARY_DIR}/local
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_lua install_dir)

add_library(lua STATIC IMPORTED GLOBAL)
set_property(TARGET lua PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/liblua${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(lua project_lua)
