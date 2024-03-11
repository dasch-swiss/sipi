project(exiv2_builder C CXX)
include(ExternalProject)

include_directories(
        ${COMMON_LOCAL}/include
        /usr/local/include
        /usr/include
)

set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)

set(Exiv2Version "v0.27.7")

ExternalProject_Add(project_exiv2
        DEPENDS expat
        DEPENDS gettext
        INSTALL_DIR ${COMMON_LOCAL}
        URL https://github.com/Exiv2/exiv2/archive/refs/tags/${Exiv2Version}.tar.gz
        ${timestamp_policy}
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        SOURCE_DIR ${COMMON_SRCS}/exiv2-trunk
        CMAKE_ARGS -DBUILD_SHARED_LIBS=OFF -DEXIV2_ENABLE_NLS=OFF -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL} -DCMAKE_INSTALL_LIBDIR=${CONFIGURE_LIBDIR}

        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_exiv2 install_dir)

if(MAKE_SHARED_SIPI)
    add_library(exiv2 SHARED IMPORTED GLOBAL)
    set_property(TARGET exiv2 PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libexiv2${CMAKE_SHARED_LIBRARY_SUFFIX})
    add_library(xmp SHARED IMPORTED GLOBAL)
    set_property(TARGET xmp PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libexiv2-xmp${CMAKE_SHARED_LIBRARY_SUFFIX})
else()
    add_library(exiv2 STATIC IMPORTED GLOBAL)
    set_property(TARGET exiv2 PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libexiv2${CMAKE_STATIC_LIBRARY_SUFFIX})
    add_library(xmp STATIC IMPORTED GLOBAL)
    set_property(TARGET xmp PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libexiv2-xmp${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()
add_dependencies(exiv2 project_exiv2)
