project(libxz_builder C CXX)
include(FetchContent)

FetchContent_Declare(
        libxz
        URL https://tukaani.org/xz/xz-5.2.5.tar.gz
        URL_HASH SHA256=f6f4910fd033078738bd82bfba4f49219d03b17eb0794eb91efbae419f4aba10
)

FetchContent_GetProperties(libxz)
if(NOT libxz_POPULATED)
    FetchContent_Populate(libxz)
    include(ExternalProject)
    ExternalProject_Add(
            libxz_build
            SOURCE_DIR ${libxz_SOURCE_DIR}
            CONFIGURE_COMMAND ${libxz_SOURCE_DIR}/configure --disable-shared --prefix=${CMAKE_BINARY_DIR}/local --libdir=${CMAKE_BINARY_DIR}/local/lib
            BUILD_COMMAND $(MAKE)
            BUILD_IN_SOURCE 1
    )
endif()

# Create an imported target
add_library(xz STATIC IMPORTED GLOBAL)
set_property(TARGET xz PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/liblzma${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(xz libxz_build)
