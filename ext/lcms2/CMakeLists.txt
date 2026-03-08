project(lcms2_builder C CXX)
include(ExternalProject)

#
# get lcms2
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

if(ZIG_STATIC_BUILD)
    set(_lcms2_shared_flags --disable-shared --enable-static)
else()
    set(_lcms2_shared_flags "")
endif()

# Local vendor fallback — offline builds when archive present
set(_local "${COMMON_VENDOR}/${DEP_LCMS2_FILENAME}")
if(EXISTS "${_local}")
    set(_lcms2_url "${_local}")
else()
    set(_lcms2_url "${DEP_LCMS2_URL}")
endif()

ExternalProject_Add(project_lcms2
        DEPENDS project_jpeg
        DEPENDS project_tiff
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL "${_lcms2_url}"
        URL_HASH SHA256=${DEP_LCMS2_SHA256}
        ${timestamp_policy}
        DOWNLOAD_DIR ""
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/lcms2
        CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/local/extsrcs/lcms2/configure
        --prefix=${CMAKE_BINARY_DIR}/local
        --with-jpeg=${CMAKE_BINARY_DIR}/local
        --with-tiff=${CMAKE_BINARY_DIR}/local
        --libdir=${CMAKE_BINARY_DIR}/local/lib
        ${_lcms2_shared_flags}
        ${ZIG_AUTOTOOLS_HOST}
        CC=${CMAKE_C_COMPILER}
        ${ZIG_EP_CPP}
        ${ZIG_EP_CFLAGS}
        BUILD_COMMAND make
        INSTALL_COMMAND make PREFIX=${CMAKE_BINARY_DIR}/local install
        BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(project_lcms2 install_dir)

add_library(lcms2 STATIC IMPORTED GLOBAL)
set_property(TARGET lcms2 PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/liblcms2${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(lcms2 project_lcms2)
