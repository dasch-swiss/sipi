project(zlib_builder C)
include(FetchContent)

# FetchContent configuration for zlib
FetchContent_Declare(
        zlib
        URL http://zlib.net/zlib-1.3.1.tar.gz
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
)

FetchContent_GetProperties(zlib)
if(NOT zlib_POPULATED)
    FetchContent_Populate(zlib)
    include(ExternalProject)
    ExternalProject_Add(
            zlib_build
            SOURCE_DIR ${zlib_SOURCE_DIR}
            CONFIGURE_COMMAND ${zlib_SOURCE_DIR}/configure --prefix=${CMAKE_BINARY_DIR}/local
            --static
            # --shared
            --libdir=${CMAKE_BINARY_DIR}/local/lib
            BUILD_COMMAND $(MAKE)
            INSTALL_COMMAND make INSTALL_LIB=${CMAKE_BINARY_DIR}/local/lib install INSTALL_TOP=${CMAKE_BINARY_DIR}/local
            BUILD_IN_SOURCE 1
    )
endif()

# Create an imported target
add_library(zlib STATIC IMPORTED GLOBAL)
set_property(TARGET zlib PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libz${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(zlib zlib_build)
