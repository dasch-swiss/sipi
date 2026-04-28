project(sqlite_builder C CXX)
include(ExternalProject)

#https://www.sqlite.org/2020/sqlite-autoconf-3310000.tar.gz
# get sqlite3
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

set(_sqlite3_shared_flags --enable-shared --enable-static)

# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_SQLITE3_FILENAME}")
if(EXISTS "${_local}")
    set(_sqlite3_url "${_local}")
else()
    set(_sqlite3_url "${DEP_SQLITE3_URL}")
endif()

ExternalProject_Add(project_sqlite3
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_sqlite3_url}"
        URL_HASH SHA256=${DEP_SQLITE3_SHA256}
        ${timestamp_policy}
        DOWNLOAD_DIR ""
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/sqlite
        CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/sqlite/configure --enable-fts5 --prefix=${CMAKE_BINARY_DIR}/local ${_sqlite3_shared_flags} --libdir=${CMAKE_BINARY_DIR}/local/lib
        CC=${CMAKE_C_COMPILER}
        "CPPFLAGS=-DSQLITE_ENABLE_RTREE=1"
        BUILD_COMMAND make
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_sqlite3 install_dir)

add_library(sqlite3 STATIC IMPORTED GLOBAL)
set_property(TARGET sqlite3 PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libsqlite3${CMAKE_STATIC_LIBRARY_SUFFIX})
set(CMAKE_CREATE_SHARED "-DBUILD_SHARED_LIBS=OFF")

add_dependencies(sqlite3 project_sqlite3)
