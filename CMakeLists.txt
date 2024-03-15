#
# Copyright © 2016 - 2023 Lukas Rosenthaler, Andrea Bianco, Benjamin Geer,
# Ivan Subotic, Tobias Schweizer, André Kilchenmann, and André Fatton.
# This file is part of Sipi.
# Sipi is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published
# by the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# Sipi is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# Additional permission under GNU AGPL version 3 section 7:
# If you modify this Program, or any covered work, by linking or combining
# it with Kakadu (or a modified version of that library) or Adobe ICC Color
# Profiles (or a modified version of that library) or both, containing parts
# covered by the terms of the Kakadu Software Licence or Adobe Software Licence,
# or both, the licensors of this Program grant you additional permission
# to convey the resulting work.
# See the GNU Affero General Public License for more details.
# You should have received a copy of the GNU Affero General Public
# License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.
#

cmake_minimum_required(VERSION 3.28)
cmake_policy(VERSION 3.18)
if(POLICY CMP0135) # DOWNLOAD_EXTRACT_TIMESTAMP
    cmake_policy(SET CMP0135 OLD)
endif()

message(STATUS "cmake version: ${CMAKE_VERSION}")

# Require C++14, but let a parent project ask for something higher
if(DEFINED CMAKE_CXX_STANDARD)
    if(CMAKE_CXX_STANDARD MATCHES "^(90|99)$" OR CMAKE_CXX_STANDARD LESS 14)
        message(FATAL_ERROR "This project requires at least C++14. Aborting...")
    endif()
else()
    set(CMAKE_CXX_STANDARD 14)
endif()
# Always enforce the language constraint
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# We need compiler extensions for some of the dependencies
if(NOT DEFINED CMAKE_CXX_EXTENSIONS)
    set(CMAKE_CXX_EXTENSIONS ON)
endif()

list(APPEND CMAKE_MESSAGE_CONTEXT sipi)
project(sipi
        VERSION 3
        LANGUAGES CXX C
)

# enables cmake's support for testing
enable_testing()

include(ExternalProject)
include(CheckStructHasMember)
include(CheckIncludeFiles)
include(FindPackageHandleStandardArgs)

# ------------------------------------------------------------------------------
# Set our path variables
# the build folder will hold all downloaded, built, and installed files
# ------------------------------------------------------------------------------
set(COMMON_LOCAL ${CMAKE_BINARY_DIR}/local)
set(COMMON_SRCS ${CMAKE_BINARY_DIR}/extsrcs)
set(CONFIGURE_LIBDIR ${COMMON_LOCAL}/lib)
set(CONFIGURE_INCDIR ${COMMON_LOCAL}/include)

# set cmake default install location
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set (CMAKE_INSTALL_PREFIX "${COMMON_LOCAL}" CACHE PATH "default install path" FORCE )
    message(STATUS CMAKE_INSTALL_DIR: ${CMAKE_INSTALL_PREFIX})
endif()

set(COMMON_SOURCE_FILES_DIR ${PROJECT_SOURCE_DIR}/src)
set(COMMON_LIBRARY_FILES_DIR ${PROJECT_SOURCE_DIR}/lib)
set(COMMON_INCLUDE_FILES_DIR ${PROJECT_SOURCE_DIR}/include)
set(DOXYGEN_FILES_DIR ${PROJECT_SOURCE_DIR}/doxygen)
set(EXT_PROJECTS_DIR ${PROJECT_SOURCE_DIR}/ext)
set(COMMON_PATCHES ${PROJECT_SOURCE_DIR}/patches)
set(COMMON_VENDOR ${PROJECT_SOURCE_DIR}/vendor)
set(SIPI_SOURCE_DIR ${PROJECT_SOURCE_DIR})

#
# Usually SIPI is compiled with all static libraries
#
option(MAKE_SHARED_SIPI "Create sipi using all shared libraries" OFF)

if(MAKE_SHARED_SIPI)
    SET(BUILD_SHARED_LIBRARIES ON)
else()
    SET(BUILD_SHARED_LIBRARIES OFF)
endif()

# Set the build type.  Options are:
#  Debug          : w/ debug symbols, w/o optimization
#  Release        : w/o debug symbols, w/ optimization
#  RelWithDebInfo : w/ debug symbols, w/ optimization
option(CMAKE_BUILD_TYPE "The default build type is RelWithDebInfo" RelWithDebInfo)


#
# Here we determine the compiler and compiler version. We need clang >= 15 or g++ >= 13
#
if(CMAKE_CXX_COMPILER_ID MATCHES "(Apple)?[Cc]lang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "15.0")
        message(FATAL_ERROR "Clang version must be 15.0.0 or greater! Aborting...")
        set(ENV(CXX) "clang++")
        set(ENV(CC) "clang")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
        message(FATAL_ERROR "Requires GCC 13.0 or greater.")
        set(ENV(CXX) "g++")
        set(ENV(CC) "gcc")
    endif()
else()
    message(WARNING "You are using an unsupported compiler (${CMAKE_CXX_COMPILER_ID}). Compilation has only been tested with Clang and GCC.")
endif()
#
# Set the C++ standard to C++14 (goal is to move to C++23)
#
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

#
# turn on some features
#
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-fvisibility-inlines-hidden" SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG)
if(SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden")
endif()
check_cxx_compiler_flag("-fvisibility=hidden" SUPPORTS_FVISIBILITY_FLAG)
if(SUPPORTS_FVISIBILITY_FLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif()


#
# Figure out the system we are compiling for and
# set flags for compiling Kakadu and Lua
#
set(DARWIN "Darwin")
set(LINUX "Linux")
math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")
if(CMAKE_SYSTEM_NAME STREQUAL DARWIN)
    set(CMAKE_MACOSX_RPATH 1)
    if (${CMAKE_HOST_SYSTEM_PROCESSOR} MATCHES "x86_64")
        set(KDU_MAKE "Makefile-Mac-x86-${BITS}-gcc")
        set(KDU_ARCH "Mac-x86-${BITS}-gcc")
        set(KDU_EXEC_PLATFORM "Mac-x86-64-gcc")
    else()
        set(KDU_MAKE "Makefile-MAC-arm-${BITS}-gcc")
        set(KDU_ARCH "Mac-arm-${BITS}-gcc")
        set(KDU_EXEC_PLATFORM "Mac-arm-64-gcc")
    endif()
    set(LUA_ARCH "macosx")
    message(STATUS "System detected as: ${CMAKE_SYSTEM_NAME}")
elseif(CMAKE_SYSTEM_NAME STREQUAL LINUX)
    if (${CMAKE_HOST_SYSTEM_PROCESSOR} MATCHES "x86_64")
        set(KDU_MAKE "Makefile-Linux-x86-${BITS}-gcc")
        set(KDU_ARCH "Linux-x86-${BITS}-gcc")
        set(KDU_EXEC_PLATFORM "Linux-x86-64-gcc")
    else()
        SET(KDU_MAKE "Makefile-Linux-arm-${BITS}-gcc")
        SET(KDU_ARCH "Linux-arm-${BITS}-gcc")
        SET(KDU_EXEC_PLATFORM "Linux-arm-64-gcc")
    endif()
    SET(LUA_ARCH "linux")
    message(STATUS "System detected as: ${CMAKE_SYSTEM_NAME}")
endif()
message(STATUS "Compiling Kakadu for: ${KDU_ARCH}")

#
# set compiler flags
#
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g -Wall -Wno-uninitialized -Wno-deprecated -Woverloaded-virtual")
elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1 -g -DNDEBUG -Wall -Wno-uninitialized -Wno-deprecated -Woverloaded-virtual")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic -g")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG -Wall -Wno-uninitialized -Wno-deprecated -Woverloaded-virtual")
endif()

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-fvisibility-inlines-hidden" SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG)
if(SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden")
endif()
check_cxx_compiler_flag("-fvisibility=hidden" SUPPORTS_FVISIBILITY_FLAG)
if(SUPPORTS_FVISIBILITY_FLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif()


#
# append path to additional find_package() cmake modules
#
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

#
# add code coverage
#
# If building the code with clang gives 'Error: could not load cache'
#   1 turn COLLECT_CODE_COVERAGE to FALSE
#   2 build and run the tests
#   3 turn COLLECT_CODE_COVERAGE back to TRUE
set(COLLECT_CODE_COVERAGE FALSE)
if (COLLECT_CODE_COVERAGE)
    include(CodeCoverage)
    append_coverage_compiler_flags()
    set(COVERAGE_EXCLUDES
            "/Applications/Xcode.app/*"
            "/opt/local/*"
            "third_party/*"
    )
endif ()

# on macOS, ignore frameworks
# the complete environment should be provided through NIX
if(CMAKE_SYSTEM_NAME STREQUAL DARWIN)
    set(CMAKE_FIND_FRAMEWORK NEVER)
    set(CMAKE_IGNORE_PATH "/usr/local/lib*")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

#
# libmagic
# (used to be build locally from source was moved to prerequisites)
#
find_package(LibMagic REQUIRED)
if (LIBMAGIC_FOUND)
    include_directories(${LibMagic_INCLUDE_DIR})
endif()

#
# openssl
# (used to be build locally from source was moved to prerequisites)
#
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL dirs: ${OPENSSL_INCLUDE_DIR}")
    message(STATUS "OpenSSL libs: ${OPENSSL_LIBRARIES}")
    include_directories(${OPENSSL_INCLUDE_DIR})
endif()

#
# libcurl
# (used to be build locally from source was moved to prerequisites)
find_package(CURL REQUIRED)
if(CURL_FOUND)
    message(STATUS "curl dirs: ${CURL_INCLUDE_DIRS}")
    message(STATUS "curl libs: ${CURL_LIBRARIES}")
    include_directories(${CURL_INCLUDE_DIRS})
endif()

#find_package(JBIG REQUIRED)
#include_directories(${JBIG_INCLUDE_DIR})
# message(STATUS "JBIG ${JBIG_INCLUDE_DIR}")

if(NOT WIN32)
  find_library(M_LIBRARY NAMES m)
  if(NOT M_LIBRARY)
    message(STATUS "math lib 'libm' not found; floating point support disabled")
  endif()
else()
  # not needed on windows
  set(M_LIBRARY "")
endif()

set(HAVE_ST_ATIMESPEC 0)
check_struct_has_member("struct stat" st_atimespec sys/stat.h HAVE_ST_ATIMESPEC LANGUAGE CXX)
if(HAVE_ST_ATIMESPEC)
    message(STATUS "Have new struct stat")
else()
    message(STATUS "Have old struct stat")
endif()

CHECK_INCLUDE_FILES (malloc.h HAVE_MALLOC_H)

#
# "standard" packages that are required for sipi
#
if(CMAKE_SYSTEM_NAME STREQUAL DARWIN)
	find_package(ICONV REQUIRED)
endif()
#find_package(MATH REQUIRED)

message(STATUS "Adding external dependencies ...")

# !!! order is important !!!
# dependent targets need to be added before the target that depends on them
# e.g., png, bzip2 before freetype
# add_subdirectory(${EXT_PROJECTS_DIR}/openssl)
add_subdirectory(${EXT_PROJECTS_DIR}/xz)
add_subdirectory(${EXT_PROJECTS_DIR}/zlib)
add_subdirectory(${EXT_PROJECTS_DIR}/zstd)
add_subdirectory(${EXT_PROJECTS_DIR}/jpeg)
add_subdirectory(${EXT_PROJECTS_DIR}/jbigkit)
add_subdirectory(${EXT_PROJECTS_DIR}/webp)
add_subdirectory(${EXT_PROJECTS_DIR}/tiff)
add_subdirectory(${EXT_PROJECTS_DIR}/expat)
add_subdirectory(${EXT_PROJECTS_DIR}/png)
add_subdirectory(${EXT_PROJECTS_DIR}/bzip2)
add_subdirectory(${EXT_PROJECTS_DIR}/gettext)
add_subdirectory(${EXT_PROJECTS_DIR}/lcms2)
add_subdirectory(${EXT_PROJECTS_DIR}/exiv2)
add_subdirectory(${EXT_PROJECTS_DIR}/jansson)
add_subdirectory(${EXT_PROJECTS_DIR}/lua)
add_subdirectory(${EXT_PROJECTS_DIR}/luarocks)
add_subdirectory(${EXT_PROJECTS_DIR}/sqlite3)
add_subdirectory(${EXT_PROJECTS_DIR}/kakadu)
# add_subdirectory(${EXT_PROJECTS_DIR}/curl)

message(STATUS "... adding external dependencies done.")

#
# add a target to generate API documentation with Doxygen
#
find_package(Doxygen)
if(DOXYGEN_FOUND)
    add_custom_target(doc
        ${DOXYGEN_EXECUTABLE} ${DOXYGEN_FILES_DIR}/Doxyfile
        WORKING_DIRECTORY ${DOXYGEN_FILES_DIR}
        COMMENT "Generating API documentation with Doxygen" VERBATIM
    )
endif(DOXYGEN_FOUND)


#
# get version from git
#
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
    find_package(Git)
    if(GIT_FOUND)
        execute_process(
                COMMAND ${GIT_EXECUTABLE} describe --tag --dirty --abbrev=7
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE BUILD_SCM_TAG
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE BUILD_SCM_REVISION
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE)
    else(GIT_FOUND)
        SET(BUILD_SCM_REVISION 0)
        SET(BUILD_SCM_TAG 0)
    endif(GIT_FOUND)
endif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)

option(BUILD_SCM_TAG "Build SCM tag" "$ENV{BUILD_TAG}")

message( STATUS "Build SCM tag: ${BUILD_SCM_TAG}" )
message( STATUS "Build SCM revision: ${BUILD_SCM_REVISION}" )

# get current date and time
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M")
message( STATUS "Build timestamp: ${BUILD_TIMESTAMP}" )

# write out the sipi configuration
configure_file(${COMMON_INCLUDE_FILES_DIR}/SipiConfig.h.in ${COMMON_INCLUDE_FILES_DIR}/SipiConfig.h)

#
# generate ICC profile includes
#
set(GENERATED_ICC_HEADERS
        ${COMMON_INCLUDE_FILES_DIR}/AdobeRGB1998_icc.h
        ${COMMON_INCLUDE_FILES_DIR}/USWebCoatedSWOP_icc.h
        ${COMMON_INCLUDE_FILES_DIR}/YCC709_icc.h
)

add_custom_command(
        OUTPUT ${GENERATED_ICC_HEADERS}
        COMMENT "Generating ICC profile includes"
        WORKING_DIRECTORY ${COMMON_INCLUDE_FILES_DIR}
        COMMAND cp ${COMMON_INCLUDE_FILES_DIR}/ICC-Profiles/AdobeRGB1998.icc ${COMMON_INCLUDE_FILES_DIR}/AdobeRGB1998.icc
        COMMAND xxd -i AdobeRGB1998.icc AdobeRGB1998_icc.h
        COMMAND rm ${COMMON_INCLUDE_FILES_DIR}/AdobeRGB1998.icc
        COMMAND cp ${COMMON_INCLUDE_FILES_DIR}/ICC-Profiles/USWebCoatedSWOP.icc ${COMMON_INCLUDE_FILES_DIR}/USWebCoatedSWOP.icc
        COMMAND xxd -i USWebCoatedSWOP.icc USWebCoatedSWOP_icc.h
        COMMAND rm ${COMMON_INCLUDE_FILES_DIR}/USWebCoatedSWOP.icc
        COMMAND cp ${COMMON_INCLUDE_FILES_DIR}/ICC-Profiles/YCC709.icm ${COMMON_INCLUDE_FILES_DIR}/YCC709.icm
        COMMAND xxd -i YCC709.icm YCC709_icc.h
        COMMAND rm ${COMMON_INCLUDE_FILES_DIR}/YCC709.icm
        COMMAND cp ${COMMON_INCLUDE_FILES_DIR}/ICC-Profiles/Rec709-Rec1886.icc ${COMMON_INCLUDE_FILES_DIR}/Rec709-Rec1886.icc
        COMMAND xxd -i Rec709-Rec1886.icc Rec709-Rec1886_icc.h
        COMMAND rm ${COMMON_INCLUDE_FILES_DIR}/Rec709-Rec1886.icc
)

add_custom_target(icc_profiles
        DEPENDS ${GENERATED_ICC_HEADERS}
)

link_directories(${CONFIGURE_LIBDIR})

include_directories(
        ${COMMON_LOCAL}/include
        ${COMMON_INCLUDE_FILES_DIR}
        ${COMMON_INCLUDE_FILES_DIR}/metadata
        ${COMMON_INCLUDE_FILES_DIR}/formats
        ${COMMON_INCLUDE_FILES_DIR}/iiifparser
        ${PROJECT_SOURCE_DIR}/shttps
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/local/include
)

add_executable(sipi
        src/sipi.cpp
        src/SipiConf.cpp include/SipiConf.h
        src/SipiError.cpp include/SipiError.h
        include/AdobeRGB1998_icc.h include/USWebCoatedSWOP_icc.h
        include/CLI11.hpp
        src/metadata/SipiIcc.cpp include/metadata/SipiIcc.h
        src/metadata/SipiXmp.cpp include/metadata/SipiXmp.h
        src/metadata/SipiIptc.cpp include/metadata/SipiIptc.h
        src/metadata/SipiExif.cpp include/metadata/SipiExif.h
        src/metadata/SipiEssentials.cpp include/metadata/SipiEssentials.h
        src/SipiImage.cpp include/SipiImage.h include/SipiImageError.h
        src/formats/SipiIOTiff.cpp include/formats/SipiIOTiff.h
        src/formats/SipiIOJ2k.cpp include/formats/SipiIOJ2k.h
        src/formats/SipiIOJpeg.cpp include/formats/SipiIOJpeg.h
        src/formats/SipiIOPng.cpp include/formats/SipiIOPng.h
        src/SipiHttpServer.cpp include/SipiHttpServer.h
        src/SipiCache.cpp include/SipiCache.h
        src/SipiLua.cpp include/SipiLua.h
        src/iiifparser/SipiRotation.cpp include/iiifparser/SipiRotation.h
        src/iiifparser/SipiQualityFormat.cpp include/iiifparser/SipiQualityFormat.h
        src/iiifparser/SipiRegion.cpp include/iiifparser/SipiRegion.h
        src/iiifparser/SipiSize.cpp include/iiifparser/SipiSize.h
        src/SipiCommon.cpp include/SipiCommon.h
        shttps/Global.h
        shttps/Error.cpp shttps/Error.h
        shttps/Hash.cpp shttps/Hash.h
        shttps/SockStream.cpp shttps/SockStream.h
        shttps/ChunkReader.cpp shttps/ChunkReader.h
        shttps/Connection.cpp shttps/Connection.h
        shttps/LuaServer.cpp shttps/LuaServer.h
        shttps/LuaSqlite.cpp shttps/LuaSqlite.h
        shttps/Parsing.cpp shttps/Parsing.h
        shttps/ThreadControl.cpp shttps/ThreadControl.h
        shttps/SocketControl.cpp shttps/SocketControl.h
        shttps/Server.cpp shttps/Server.h
        shttps/jwt.c shttps/jwt.h
        shttps/makeunique.h
        src/SipiFilenameHash.cpp include/SipiFilenameHash.h
        include/iiifparser/SipiIdentifier.h src/iiifparser/SipiIdentifier.cpp
        include/SipiImageError.h)

add_dependencies(sipi icc_profiles)

# !!! ORDER MATTERS !!!
# dependent targets need to be added before the target that depends on them
target_link_libraries(sipi ${LIBS}
        lcms2
        exiv2
        exiv2-xmp
        expat
        tiff
        webp
        jbigkit
        png
        kdu_aux
        kdu
        xz
        magic
        lua
        jansson
        sqlite3
        dl
        pthread
        # curl
        ${CURL_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        # ssl
        # crypto
        jpeg
        bzip2
        zstd
        z
        m
        lzma
        Threads::Threads
        ${CMAKE_DL_LIBS}
)

if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
	target_link_libraries(sipi iconv gettext_intl)
else()
    target_link_libraries(sipi rt)
endif()


install(TARGETS sipi
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/local/bin/lua
        ${CMAKE_CURRENT_SOURCE_DIR}/local/bin/luarocks
        DESTINATION ${CMAKE_INSTALL_PREFIX}/bin/
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/config/sipi.config.lua
        DESTINATION ${CMAKE_INSTALL_PREFIX}/etc
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)


#-------------------
# Test
#-------------------

# adds google tests defined in the test/unit and test/approval subdirectories
add_subdirectory(test)

# custom target to only run 'e2e' tests
add_custom_target(e2e
        DEPENDS sipi
        COMMAND pytest -s --sipi-exec=${CMAKE_BINARY_DIR}/sipi
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test/e2e)

# enables running of 'e2e' tests when `make test` is executed
add_test(NAME all_python_based_e2e_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test/e2e
        COMMAND pytest -s --sipi-exec=$<TARGET_FILE:sipi>)
