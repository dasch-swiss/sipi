project(sqlite_builder C CXX)
include(ExternalProject)

#https://www.sqlite.org/2020/sqlite-autoconf-3310000.tar.gz
# get sqlite3
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

ExternalProject_Add(project_sqlite3
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "https://www.sqlite.org/2024/sqlite-autoconf-3450200.tar.gz"
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/sqlite
        CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/sqlite/configure --enable-fts5 --prefix=${CMAKE_BINARY_DIR}/local --enable-shared --enable-static --libdir=${CMAKE_BINARY_DIR}/local/lib
        BUILD_COMMAND make CFLAGS=-DSQLITE_ENABLE_RTREE=1
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_sqlite3 install_dir)

add_library(sqlite3 STATIC IMPORTED GLOBAL)
set_property(TARGET sqlite3 PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libsqlite3${CMAKE_STATIC_LIBRARY_SUFFIX})
set(CMAKE_CREATE_SHARED "-DBUILD_SHARED_LIBS=OFF")

add_dependencies(sqlite3 project_sqlite3)
