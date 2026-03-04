project(sqlite_builder C CXX)
include(ExternalProject)

#https://www.sqlite.org/2020/sqlite-autoconf-3310000.tar.gz
# get sqlite3
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

if(ZIG_STATIC_BUILD)
    set(_sqlite3_shared_flags --disable-shared --enable-static)
else()
    set(_sqlite3_shared_flags --enable-shared --enable-static)
endif()

ExternalProject_Add(project_sqlite3
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "https://www.sqlite.org/2024/sqlite-autoconf-3450200.tar.gz"
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        ${timestamp_policy}
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/sqlite
        CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/sqlite/configure --enable-fts5 --prefix=${CMAKE_BINARY_DIR}/local ${_sqlite3_shared_flags} --libdir=${CMAKE_BINARY_DIR}/local/lib
        ${ZIG_AUTOTOOLS_HOST}
        CC=${CMAKE_C_COMPILER}
        ${ZIG_EP_CPP}
        ${ZIG_EP_CFLAGS}
        "CPPFLAGS=-DSQLITE_ENABLE_RTREE=1"
        BUILD_COMMAND make
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_sqlite3 install_dir)

add_library(sqlite3 STATIC IMPORTED GLOBAL)
set_property(TARGET sqlite3 PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libsqlite3${CMAKE_STATIC_LIBRARY_SUFFIX})
set(CMAKE_CREATE_SHARED "-DBUILD_SHARED_LIBS=OFF")

add_dependencies(sqlite3 project_sqlite3)
