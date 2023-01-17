cmake_minimum_required(VERSION 3.14.5)
project(liblua_builder C CXX)
include(ExternalProject)


#
# get lua
#
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.25.1)
    set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)
else()
    unset(timestamp_policy)
endif()


ExternalProject_Add(project_lua
    INSTALL_DIR ${COMMON_LOCAL}
    URL http://www.lua.org/ftp/lua-5.3.5.tar.gz
        ${timestamp_policy}
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        #DOWNLOAD_EXTRACT_TIMESTAMP NEW
    SOURCE_DIR ${COMMON_SRCS}/lua-5.3.5
    CONFIGURE_COMMAND ls
#    BUILD_COMMAND make CPPFLAGS=-DLUA_USE_APICHECK CFLAGS=-DLUA_USE_APICHECK INSTALL_LIB=${CONFIGURE_LIBDIR} ${LUA_ARCH}
    BUILD_COMMAND make INSTALL_LIB=${CONFIGURE_LIBDIR} ${LUA_ARCH}
    INSTALL_COMMAND make INSTALL_LIB=${CONFIGURE_LIBDIR} install INSTALL_TOP=${COMMON_LOCAL}
    BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_lua install_dir)

add_library(lua STATIC IMPORTED GLOBAL)
set_property(TARGET lua PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/liblua${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(lua project_lua)
