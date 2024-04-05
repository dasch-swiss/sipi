project(libluarocks_builder C CXX)
include(ExternalProject)

#
# get luarocks
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)


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
        --with-lua-bin=${CMAKE_BINARY_DIR}/local/bin
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
