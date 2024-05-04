project(kakadu_builder C CXX)
include(ExternalProject)

#
# Figure out the system we are compiling for and
# set flags for compiling Kakadu and Lua (TODO: move kakadu and lua configuration to their own files)
#
set(DARWIN "Darwin")
set(LINUX "Linux")
math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")
if (CMAKE_SYSTEM_NAME STREQUAL DARWIN)
    set(CMAKE_MACOSX_RPATH 1)
    if (${CMAKE_HOST_SYSTEM_PROCESSOR} MATCHES "x86_64")
        set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Build architectures for OSX" FORCE)
        set(KDU_MAKE "Makefile-Mac-x86-${BITS}-gcc")
        set(KDU_ARCH "Mac-x86-${BITS}-gcc")
        set(KDU_EXEC_PLATFORM "Mac-x86-64-gcc")
    else ()
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for OSX" FORCE)
        set(KDU_MAKE "Makefile-MAC-arm-${BITS}-gcc")
        set(KDU_ARCH "Mac-arm-${BITS}-gcc")
        set(KDU_EXEC_PLATFORM "Mac-arm-64-gcc")
    endif ()
    set(LUA_ARCH "macosx")
elseif (CMAKE_SYSTEM_NAME STREQUAL LINUX)
    if (${CMAKE_HOST_SYSTEM_PROCESSOR} MATCHES "x86_64")
        set(KDU_MAKE "Makefile-Linux-x86-${BITS}-gcc")
        set(KDU_ARCH "Linux-x86-${BITS}-gcc")
        set(KDU_EXEC_PLATFORM "Linux-x86-64-gcc")
    else ()
        SET(KDU_MAKE "Makefile-Linux-arm-${BITS}-gcc")
        SET(KDU_ARCH "Linux-arm-${BITS}-gcc")
        SET(KDU_EXEC_PLATFORM "Linux-arm-64-gcc")
    endif ()
    SET(LUA_ARCH "linux")
endif ()

message(STATUS "System detected as: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiling Kakadu for: ${KDU_ARCH}")

message(STATUS ${CMAKE_BINARY_DIR}/local/lib)
#
# compiling kakadu (without JAVA interface)
#

message(STATUS "VENDOR=${COMMON_VENDOR}")
message(STATUS "KDU_MAKE=${KDU_MAKE}")

set(KakaduVersion "v8_4_1-01382N")

ExternalProject_Add(project_kakadu
        INSTALL_DIR ${CMAKE_BINARY_DIR}/local
        DOWNLOAD_COMMAND unzip -o -d ${CMAKE_BINARY_DIR}/extsrcs/kakadu ${KakaduVersion}.zip
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
