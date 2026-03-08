project(libjansson_builder C CXX)
include(ExternalProject)

set(JANSSON_INSTALL_LIB_DIR "lib/${BITS}")

#
# get jansson
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)


# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_JANSSON_FILENAME}")
if(EXISTS "${_local}")
    set(_jansson_url "${_local}")
else()
    set(_jansson_url "${DEP_JANSSON_URL}")
endif()

ExternalProject_Add(project_jansson
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_jansson_url}"
        URL_HASH SHA256=${DEP_JANSSON_SHA256}
        ${timestamp_policy}
        DOWNLOAD_DIR ""
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/jansson
        CMAKE_ARGS ${ZIG_EP_CMAKE_ARGS}
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DJANSSON_BUILD_DOCS=OFF
        -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
        -DJANSSON_INSTALL_LIB_DIR:String=${CMAKE_BINARY_DIR}/local/lib
)
ExternalProject_Get_Property(project_jansson install_dir)

add_library(jansson STATIC IMPORTED GLOBAL)
set_property(TARGET jansson PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libjansson${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(jansson project_jansson)
