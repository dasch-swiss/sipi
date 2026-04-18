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

# Lua's hand-written Makefile doesn't use CMake, so the toolchain-driven
# `.zig-target` marker picked up by our zig-cc wrapper can be missed in
# some call paths. Passing ZIG_TARGET explicitly in the env guarantees
# the wrapper sees it and injects `-target` — crucial for cross-compiling
# to musl, otherwise glibc headers expand `fopen` → `fopen64` etc. and
# the final link fails with undefined *64 symbols.
if(DEFINED ZIG_TARGET AND NOT ZIG_TARGET STREQUAL "")
    set(_LUA_ENV ${CMAKE_COMMAND} -E env ZIG_TARGET=${ZIG_TARGET})
else()
    set(_LUA_ENV "")
endif()

ExternalProject_Add(project_lua
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_lua_url}"
        URL_HASH SHA256=${DEP_LUA_SHA256}
        ${timestamp_policy}
        DOWNLOAD_DIR ""
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/lua-${DEP_LUA_VERSION}
        CONFIGURE_COMMAND ls
        BUILD_COMMAND ${_LUA_ENV} make CC=${CMAKE_C_COMPILER} "MYCFLAGS=${CMAKE_C_FLAGS}" INSTALL_LIB=${CMAKE_BINARY_DIR}/local/lib ${LUA_ARCH}
        INSTALL_COMMAND ${_LUA_ENV} make CC=${CMAKE_C_COMPILER} "MYCFLAGS=${CMAKE_C_FLAGS}" INSTALL_LIB=${CMAKE_BINARY_DIR}/local/lib install INSTALL_TOP=${CMAKE_BINARY_DIR}/local
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_lua install_dir)

add_library(lua STATIC IMPORTED GLOBAL)
set_property(TARGET lua PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/liblua${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(lua project_lua)
