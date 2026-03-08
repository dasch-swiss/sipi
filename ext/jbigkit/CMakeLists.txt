project(jbigkit_builder C CXX)
include(ExternalProject)

#
# get JBIG
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)


# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_JBIGKIT_FILENAME}")
if(EXISTS "${_local}")
    set(_jbigkit_url "${_local}")
else()
    set(_jbigkit_url "${DEP_JBIGKIT_URL}")
endif()

ExternalProject_Add(project_jbigkit
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_jbigkit_url}"
        URL_HASH SHA256=${DEP_JBIGKIT_SHA256}
        ${timestamp_policy}
        DOWNLOAD_DIR ""
        SOURCE_DIR ${CMAKE_BINARY_DIR}/extsrcs/jbigkit-${DEP_JBIGKIT_VERSION}
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make CC=${CMAKE_C_COMPILER} ${ZIG_EP_CFLAGS}
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/extsrcs/jbigkit-${DEP_JBIGKIT_VERSION}/libjbig/libjbig${CMAKE_STATIC_LIBRARY_SUFFIX} ${CMAKE_BINARY_DIR}/local/lib
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_jbigkit install_dir)

add_library(jbigkit STATIC IMPORTED GLOBAL)
set_property(TARGET jbigkit PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libjbig${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(jbigkit project_jbigkit)
