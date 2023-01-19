cmake_minimum_required(VERSION 3.14.5)
project(exiv2_builder C CXX)
include(ExternalProject)

include_directories(
        ${COMMON_LOCAL}/include
        /usr/local/include
        /usr/include
)

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.24.0)
    set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)
else()
    unset(timestamp_policy)
endif()

ExternalProject_Add(project_exiv2
        DEPENDS expat
        DEPENDS gettext
        INSTALL_DIR ${COMMON_LOCAL}
        #URL https://dl.bintray.com/dhlab-basel/thirdparty/exiv2-0.26-trunk.tar.gz
        #URL_HASH SHA256=c75e3c4a0811bf700d92c82319373b7a825a2331c12b8b37d41eb58e4f18eafb
        URL https://github.com/Exiv2/exiv2/releases/download/v0.27.5/exiv2-0.27.5-Source.tar.gz
        ${timestamp_policy}
        DOWNLOAD_DIR ${COMMON_SRCS}/downloads
        #DOWNLOAD_EXTRACT_TIMESTAMP NEW
        SOURCE_DIR ${COMMON_SRCS}/exiv2-trunk

        #CMAKE_ARGS -DBUILD_SHARED_LIBS=OFF -DEXIV2_ENABLE_VIDEO=ON -DCMAKE_MODULE_PATH=${COMMON_LOCAL} -DCMAKE_INSTALL_PREFIX=${COMMON_LOCAL} -DCMAKE_INSTALL_LIBDIR=${CONFIGURE_LIBDIR}
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
