project(sqlite_builder C)
include(FetchContent)

# Fetch SQLite source code using FetchContent
FetchContent_Declare(
        sqlite
        URL "https://www.sqlite.org/2024/sqlite-autoconf-3450200.tar.gz"
        URL_HASH SHA256=bc9067442eedf3dd39989b5c5cfbfff37ae66cc9c99274e0c3052dc4d4a8f6ae
)

# Check if SQLite has been fetched and if not, populate it
FetchContent_GetProperties(sqlite)
if(NOT sqlite_POPULATED)
    FetchContent_Populate(sqlite)
    include(ExternalProject)
    ExternalProject_Add(
            project_sqlite3
            SOURCE_DIR ${sqlite_SOURCE_DIR}
            CONFIGURE_COMMAND ${sqlite_SOURCE_DIR}/configure --enable-fts5 --prefix=${CMAKE_BINARY_DIR}/local --enable-static --libdir=${CMAKE_BINARY_DIR}/local/lib
            BUILD_COMMAND make CFLAGS=-DSQLITE_ENABLE_RTREE=1
            INSTALL_COMMAND make install
            BUILD_IN_SOURCE 1
    )
endif()

# Define the SQLite library as an IMPORTED target
add_library(sqlite3 STATIC IMPORTED GLOBAL)
set_target_properties(sqlite3 PROPERTIES
        IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libsqlite3${CMAKE_STATIC_LIBRARY_SUFFIX}
)

# Ensure that the sqlite3 library target depends on the completed build of SQLite
add_dependencies(sqlite3 project_sqlite3 libz)
