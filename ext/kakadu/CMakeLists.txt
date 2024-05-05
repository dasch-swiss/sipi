project(kakadu_builder C CXX)
include(ExternalProject)

message(STATUS ${CMAKE_BINARY_DIR}/local/lib)
#
# compiling kakadu (without JAVA interface)
#

message(STATUS "VENDOR=${COMMON_VENDOR}")
message(STATUS "KDU_MAKE=${KDU_MAKE}")

set(KakaduVersion "v8_4_1-01382N")

ExternalProject_Add(project_kakadu
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        DOWNLOAD_COMMAND unzip -o -d ${CMAKE_BINARY_DIR}/extsrcs/kakadu ${COMMON_VENDOR}/${KakaduVersion}.zip
        DOWNLOAD_EXTRACT_TIMESTAMP OLD
        DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/extsrcs/kakadu
        PATCH_COMMAND ""
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make -j1 --directory=${CMAKE_BINARY_DIR}/extsrcs/kakadu/${KakaduVersion}/make/ -f ${KDU_MAKE} EXEC_PLATFORM=${KDU_EXEC_PLATFORM} all_but_jni
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/extsrcs/kakadu/${KakaduVersion}/lib/${KDU_ARCH} ${CMAKE_BINARY_DIR}/local/lib
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/extsrcs/kakadu/${KakaduVersion}/bin/${KDU_ARCH} ${CMAKE_BINARY_DIR}/local/bin
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/extsrcs/kakadu/${KakaduVersion}/managed/all_includes ${CMAKE_BINARY_DIR}/local/include
)

ExternalProject_Get_Property(project_kakadu install_dir)

add_library(kdu STATIC IMPORTED GLOBAL)
set_property(TARGET kdu PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libkdu${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(kdu project_kakadu)

add_library(kdu_aux STATIC IMPORTED GLOBAL)
set_property(TARGET kdu_aux PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/local/lib/libkdu_aux${CMAKE_STATIC_LIBRARY_SUFFIX})

add_dependencies(kdu_aux project_kakadu)
