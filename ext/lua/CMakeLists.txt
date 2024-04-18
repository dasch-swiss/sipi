project(liblua_builder C CXX)
include(FetchContent)

# FetchContent configuration for Lua
FetchContent_Declare(
        lua
        URL http://www.lua.org/ftp/lua-5.4.6.tar.gz
        URL_HASH SHA256=7d5ea1b9cb6aa0b59ca3dde1c6adcb57ef83a1ba8e5432c0ecd06bf439b3ad88
)

# Populate Lua content
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
    # Using ExternalProject to build Lua as Lua does not natively use CMake
    include(ExternalProject)
    ExternalProject_Add(
            project_lua
            SOURCE_DIR ${lua_SOURCE_DIR}
            CONFIGURE_COMMAND ""
            BUILD_COMMAND make CC=clang INSTALL_TOP=${CMAKE_BINARY_DIR}/local ${LUA_ARCH}
            INSTALL_COMMAND make install INSTALL_TOP=${CMAKE_BINARY_DIR}/local
            BUILD_IN_SOURCE 1
    )
endif()

# Define the Lua library as an IMPORTED target
add_library(lua STATIC IMPORTED GLOBAL)
set_target_properties(lua PROPERTIES
        IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/liblua${CMAKE_STATIC_LIBRARY_SUFFIX}
)

# Ensure that the Lua library target depends on the completed build of Lua
add_dependencies(lua project_lua)
