project(libjansson_builder C CXX)
include(ExternalProject)

ExternalProject_Add(project_jansson
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://github.com/akheron/jansson/releases/download/v2.13.1/jansson-2.13.1.tar.gz
        URL_HASH SHA256=f4f377da17b10201a60c1108613e78ee15df6b12016b116b6de42209f47a474f
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/local/extsrcs/jansson
        CMAKE_ARGS -DJANSSON_BUILD_DOCS=OFF
        -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/local
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/local
        -DJANSSON_INSTALL_LIB_DIR:String=${CMAKE_BINARY_DIR}/local/lib
)
ExternalProject_Get_Property(project_jansson install_dir)

add_library(jansson STATIC IMPORTED GLOBAL)
set_property(TARGET jansson PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libjansson${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(jansson project_jansson)
