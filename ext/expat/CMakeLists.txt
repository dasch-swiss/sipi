project(expat_builder C CXX)
include(ExternalProject)

set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_EXPAT_FILENAME}")
if(EXISTS "${_local}")
    set(_expat_url "${_local}")
else()
    set(_expat_url "${DEP_EXPAT_URL}")
endif()

ExternalProject_Add(project_expat
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_expat_url}"
        URL_HASH SHA256=${DEP_EXPAT_SHA256}
        ${timestamp_policy}
        DOWNLOAD_DIR ""
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/expat
        CMAKE_ARGS -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
        -DCMAKE_INSTALL_LIBDIR=lib
        -DEXPAT_BUILD_TOOLS:BOOL=OFF
        -DEXPAT_BUILD_EXAMPLES:BOOL=OFF
        -DEXPAT_BUILD_DOCS:BOOL=OFF
        -DBUILD_SHARED_LIBS:BOOL=${MAKE_SHARED_SIPI}
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_expat install_dir)

add_library(expat STATIC IMPORTED GLOBAL)
set_property(TARGET expat PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libexpat${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(expat project_expat)
