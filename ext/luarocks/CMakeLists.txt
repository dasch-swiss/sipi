cmake_minimum_required(VERSION 3.14.5)
project(libluarocks_builder C CXX)
include(ExternalProject)

#
# get luarocks
#
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.25.1)
    set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)
else()
    unset(timestamp_policy)
endif()

ExternalProject_Add(project_luarocks
        DEPENDS lua
        INSTALL_DIR ${COMMON_LOCAL}
        URL http://luarocks.github.io/luarocks/releases/luarocks-3.3.1.tar.gz
        ${timestamp_policy}
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        #DOWNLOAD_EXTRACT_TIMESTAMP NEW
        SOURCE_DIR ${COMMON_SRCS}/luarocks-3.3.1
        CONFIGURE_COMMAND   ${COMMON_SRCS}/luarocks-3.3.1/configure
                            --prefix=${COMMON_LOCAL}
                            --with-lua-bin=${COMMON_LOCAL}/bin
                            --with-lua-include=${COMMON_LOCAL}/include
                            --with-lua-lib=${COMMON_LOCAL}/lib
                            --rocks-tree=${CMAKE_INSTALL_PREFIX}/luarocks
#    --libdir=${CONFIGURE_LIBDIR}
        BUILD_COMMAND make build
        INSTALL_COMMAND make install
        BUILD_IN_SOURCE 1
        )
ExternalProject_Get_Property(project_luarocks install_dir)

add_library(luarocks STATIC IMPORTED GLOBAL)

add_dependencies(luarocks project_luarocks)
