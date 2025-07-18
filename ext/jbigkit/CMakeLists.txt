project(jbigkit_builder C CXX)
include(ExternalProject)

#
# get JBIG
#
set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)


ExternalProject_Add(project_jbigkit
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        URL https://www.cl.cam.ac.uk/~mgk25/jbigkit/download/jbigkit-2.1.tar.gz
        ${timestamp_policy}
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/extsrcs/downloads
        SOURCE_DIR ${CMAKE_BINARY_DIR}/extsrcs/jbigkit-2.1
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make CC=${CMAKE_C_COMPILER}
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/extsrcs/jbigkit-2.1/libjbig/libjbig${CMAKE_STATIC_LIBRARY_SUFFIX} ${CMAKE_BINARY_DIR}/local/lib
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_jbigkit install_dir)

add_library(jbigkit STATIC IMPORTED GLOBAL)
set_property(TARGET jbigkit PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libjbig${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(jbigkit project_jbigkit)
