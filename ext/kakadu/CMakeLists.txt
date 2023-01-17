cmake_minimum_required(VERSION 3.14.5)
project(kakadu_builder C CXX)
include(ExternalProject)

message(STATUS ${CONFIGURE_LIBDIR})
#
# compiling kakadu (without JAVA interface)
#

message(STATUS "VENDOR=${COMMON_VENDOR}")
message(STATUS "KDU_MAKE=${KDU_MAKE}")

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.25.1)
    set(timestamp_policy DOWNLOAD_EXTRACT_TIMESTAMP OLD)
else()
    unset(timestamp_policy)
endif()

ExternalProject_Add(
        project_kakadu
        INSTALL_DIR ${COMMON_LOCAL}
        DOWNLOAD_COMMAND unzip -o -d ${COMMON_SRCS} ${COMMON_VENDOR}/v8_3-01727L.zip
        ${timestamp_policy}
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make --directory=${COMMON_SRCS}/v8_3-01727L/make/ -f ${KDU_MAKE} EXEC_PLATFORM=${KDU_EXEC_PLATFORM} all_but_jni
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory ${COMMON_SRCS}/v8_3-01727L/lib/${KDU_ARCH} ${CONFIGURE_LIBDIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${COMMON_SRCS}/v8_3-01727L/bin/${KDU_ARCH} ${COMMON_LOCAL}/bin
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${COMMON_SRCS}/v8_3-01727L/managed/all_includes ${COMMON_LOCAL}/include
)

ExternalProject_Get_Property(project_kakadu install_dir)

if(MAKE_SHARED_SIPI)
    add_library(kdu SHARED IMPORTED GLOBAL)
    set_property(TARGET kdu PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libkdu_v8AR.so)
else()
    add_library(kdu STATIC IMPORTED GLOBAL)
    set_property(TARGET kdu PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libkdu${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()

add_dependencies(kdu project_kakadu)

if(MAKE_SHARED_SIPI)
    add_library(kdu_aux SHARED IMPORTED GLOBAL)
    set_property(TARGET kdu_aux PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libkdu_a8AR.so)
else()
    add_library(kdu_aux STATIC IMPORTED GLOBAL)
    set_property(TARGET kdu_aux PROPERTY IMPORTED_LOCATION ${CONFIGURE_LIBDIR}/libkdu_aux${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()

add_dependencies(kdu_aux project_kakadu)
