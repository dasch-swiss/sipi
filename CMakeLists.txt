#
# Copyright © 2016 Lukas Rosenthaler, Andrea Bianco, Benjamin Geer,
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

cmake_minimum_required(VERSION 3.14.5)

include(ExternalProject)
include(CheckStructHasMember)
include(CheckIncludeFiles)

project(sipi)

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
option(MAKE_DEBUG "Create sipi with debugger options on" ON)

# check to see if we have access to ccache
find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
endif(CCACHE_FOUND)

#
# Here we determine the compiler and compiler version. We need clang >= 7.3 or g++ >= 5.3
#
if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "10.0")
        message(FATAL_ERROR "Requires Apple Clang 10.0 or greater.")
        set(ENV(CXX) "clang++")
        set(ENV(CC) "clang")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "7.0.0")
        message(FATAL_ERROR "Requires Clang 7.0.0 or greater.")
        set(ENV(CXX) "clang++")
        set(ENV(CC) "clang")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "7.0")
        message(FATAL_ERROR "Requires GCC 7.0 or greater.")
        set(ENV(CXX) "g++")
        set(ENV(CC) "gcc")
    endif()
else()
    message(WARNING "You are using an unsupported compiler (${CMAKE_CXX_COMPILER_ID}). Compilation has only been tested with Clang and GCC.")
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


set(DARWIN "Darwin")
set(LINUX "Linux")
math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")
if(CMAKE_SYSTEM_NAME STREQUAL DARWIN)
    message(STATUS "Compiling for Darwin...")
    message(STATUS ${CMAKE_SYSTEM_NAME})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
    SET(CMAKE_MACOSX_RPATH 1)
    SET(KDU_MAKE "Makefile-MAC-x86-${BITS}-gcc")
    SET(KDU_ARCH "Mac-x86-${BITS}-gcc")
    SET(LUA_ARCH "macosx")
    # ToDo: shouldn't this be also set on the mac through FindOpenSSL?
    SET(OPENSSL_ROOT_DIR "/usr/local/opt/openssl" CACHE FILEPATH "openssl")
    SET(OPENSSL_USE_STATIC_LIBS ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL LINUX)
    message(STATUS "Compiling for Linux64...")
    message(STATUS ${CMAKE_SYSTEM_NAME})
    execute_process(COMMAND /usr/bin/lsb_release -si COMMAND tr -d '\n' OUTPUT_VARIABLE CMAKE_DISTRO_NAME)
    MESSAGE(STATUS "Distro: " ${CMAKE_DISTRO_NAME})
    SET(KDU_MAKE "Makefile-Linux-x86-${BITS}-gcc")
    SET(KDU_ARCH "Linux-x86-${BITS}-gcc")
    SET(LUA_ARCH "linux")
endif()
message(STATUS "Compiling for ${KDU_ARCH}")

#
# OpenSSL ??
#
include(FindOpenSSL)
if(OPENSSL_FOUND)
    message(STATUS "Found OpenSSL at ${OPENSSL_ROOT_DIR}")
    add_definitions(${CMAKE_C_FLAGS} " -DSHTTPS_ENABLE_SSL")
    add_definitions(${CMAKE_C_FLAGS} " -I${OPENSSL_INCLUDE_DIR}")
    message(STATUS "OPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}")
    message(STATUS "OPENSSL_VERSION=${OPENSSL_VERSION}")
else()
    message(STATUS "No SSL support found!")
endif()

#SET(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
if(MAKE_SHARED_SIPI)
    SET(BUILD_SHARED_LIBRARIES ON)
#    SET(CMAKE_EXE_LINKER_FLAGS "-shared")
else()
    SET(BUILD_SHARED_LIBRARIES OFF)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL DARWIN)
#        SET(CMAKE_EXE_LINKER_FLAGS "-static")
    endif()
endif()

#
# set path to special find_package() code
#
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

if(MAKE_DEBUG)
    set(CMAKE_CXX_FLAGS "-g -DNDEBUG -std=c++11 -Wall -Wno-uninitialized -Wno-deprecated -Woverloaded-virtual")
else()
    set(CMAKE_CXX_FLAGS "-O3 -DNDEBUG -std=c++11 -Wall -Wno-uninitialized -Wno-deprecated -Woverloaded-virtual")
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
# libmagic
# (used to be build locally from source was moved to prerequisites)
#
find_package(LibMagic REQUIRED)

#find_package(JBIG REQUIRED)
#include_directories(${JBIG_INCLUDE_DIR})
# message(STATUS "JBIG ${JBIG_INCLUDE_DIR}")

if(NOT WIN32)
  find_library(M_LIBRARY
    NAMES m
    PATHS /usr/lib /usr/local/lib
  )
  if(NOT M_LIBRARY)
    message(STATUS "math lib 'libm' not found; floating point support disabled")
  endif()
else()
  # not needed on windows
  set(M_LIBRARY "")
endif()

CHECK_STRUCT_HAS_MEMBER("struct stat" st_atimespec sys/stat.h HAVE_ST_ATIMESPEC LANGUAGE CXX)
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

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# !!! order is important !!!
# dependent targets need to be added before the dependers,
# e.g., png, bzip2 before freetype
add_subdirectory(${EXT_PROJECTS_DIR}/xz)
add_subdirectory(${EXT_PROJECTS_DIR}/zlib)
add_subdirectory(${EXT_PROJECTS_DIR}/jpeg)
add_subdirectory(${EXT_PROJECTS_DIR}/jbigkit)
add_subdirectory(${EXT_PROJECTS_DIR}/webp)
add_subdirectory(${EXT_PROJECTS_DIR}/tiff)
add_subdirectory(${EXT_PROJECTS_DIR}/expat)
add_subdirectory(${EXT_PROJECTS_DIR}/png)
add_subdirectory(${EXT_PROJECTS_DIR}/bzip2)
add_subdirectory(${EXT_PROJECTS_DIR}/freetype)
add_subdirectory(${EXT_PROJECTS_DIR}/harfbuzz)
add_subdirectory(${EXT_PROJECTS_DIR}/fontconfig)
add_subdirectory(${EXT_PROJECTS_DIR}/unistring)
add_subdirectory(${EXT_PROJECTS_DIR}/podofo)
add_subdirectory(${EXT_PROJECTS_DIR}/gettext)
add_subdirectory(${EXT_PROJECTS_DIR}/lcms2)
add_subdirectory(${EXT_PROJECTS_DIR}/exiv2)
add_subdirectory(${EXT_PROJECTS_DIR}/jansson)
add_subdirectory(${EXT_PROJECTS_DIR}/lua)
add_subdirectory(${EXT_PROJECTS_DIR}/luarocks)
add_subdirectory(${EXT_PROJECTS_DIR}/sqlite3)
add_subdirectory(${EXT_PROJECTS_DIR}/kakadu)
add_subdirectory(${EXT_PROJECTS_DIR}/curl)
add_subdirectory(${EXT_PROJECTS_DIR}/poppler)

# add a target to generate API documentation with Doxygen
find_package(Doxygen)
if(DOXYGEN_FOUND)
    add_custom_target(doc
        ${DOXYGEN_EXECUTABLE} ${DOXYGEN_FILES_DIR}/Doxyfile
        WORKING_DIRECTORY ${DOXYGEN_FILES_DIR}
        COMMENT "Generating API documentation with Doxygen" VERBATIM
    )
endif(DOXYGEN_FOUND)

# get current date and time
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M")
MESSAGE( STATUS "Build timestamp: ${BUILD_TIMESTAMP}" )

# get version from git
IF(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
    FIND_PACKAGE(Git)
    IF(GIT_FOUND)
        EXECUTE_PROCESS(
                COMMAND ${GIT_EXECUTABLE} describe --tag --dirty --abbrev=7
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE "BUILD_SCM_TAG"
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        MESSAGE( STATUS "Build SCM tag: ${BUILD_SCM_TAG}" )
        EXECUTE_PROCESS(
                COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE "BUILD_SCM_REVISION"
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        MESSAGE( STATUS "Build SCM revision: ${BUILD_SCM_REVISION}" )
    ELSE(GIT_FOUND)
        SET(BUILD_SCM_REVISION 0)
        SET(BUILD_SCM_TAG 0)
    ENDIF(GIT_FOUND)
ENDIF(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)

CONFIGURE_FILE(${COMMON_INCLUDE_FILES_DIR}/SipiConfig.h.in ${COMMON_INCLUDE_FILES_DIR}/SipiConfig.h)


#
# Here we get the ICC profiles from the Adobe website and prepare them for including
# We just use the AdobeRGB1998.cc and the USWebCoatedSWOP.icc profiles for now...
#
#unset(ICC CACHE)
#find_file(
#    ICC
#    icc.zip
#    ${COMMON_INCLUDE_FILES_DIR}
#    NO_DEFAULT_PATH
#)
#if(ICC)
#    message(STATUS "Adobe ICC profiles were already downloaded")
#else()
#    file(DOWNLOAD http://download.adobe.com/pub/adobe/iccprofiles/win/AdobeICCProfilesWin_end-user.zip ${COMMON_INCLUDE_FILES_DIR}/icc.zip SHOW_PROGRESS)
#endif()

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

#add_custom_command(
#        OUTPUT ${ICC_HEADERS}
#        COMMENT "Generating ICC profile includes"
#        WORKING_DIRECTORY ${COMMON_INCLUDE_FILES_DIR}
#        DEPENDS ${COMMON_INCLUDE_FILES_DIR}/icc.zip
#        COMMAND unzip -o ${COMMON_INCLUDE_FILES_DIR}/icc.zip -d ${COMMON_INCLUDE_FILES_DIR}/
#        COMMAND cp '${COMMON_INCLUDE_FILES_DIR}/Adobe ICC Profiles \(end-user\)/RGB Profiles/AdobeRGB1998.icc' ${COMMON_INCLUDE_FILES_DIR}/AdobeRGB1998.icc
#        COMMAND xxd -i AdobeRGB1998.icc AdobeRGB1998_icc.h
#        COMMAND rm ${COMMON_INCLUDE_FILES_DIR}/AdobeRGB1998.icc
#        COMMAND cp '${COMMON_INCLUDE_FILES_DIR}/Adobe ICC Profiles \(end-user\)/CMYK Profiles/USWebCoatedSWOP.icc' ${COMMON_INCLUDE_FILES_DIR}/USWebCoatedSWOP.icc
#        COMMAND xxd -i USWebCoatedSWOP.icc USWebCoatedSWOP_icc.h
#        COMMAND rm ${COMMON_INCLUDE_FILES_DIR}/USWebCoatedSWOP.icc
#        COMMAND rm -rf '${COMMON_INCLUDE_FILES_DIR}/Adobe ICC Profiles \(end-user\)'
#)
add_custom_target(icc_profiles
        DEPENDS ${GENERATED_ICC_HEADERS}
)

link_directories(
        /usr/local/lib
        ${CONFIGURE_LIBDIR}
)

include_directories(
        ${COMMON_INCLUDE_FILES_DIR}
        ${COMMON_INCLUDE_FILES_DIR}/metadata
        ${COMMON_INCLUDE_FILES_DIR}/formats
        ${COMMON_INCLUDE_FILES_DIR}/iiifparser
        ${PROJECT_SOURCE_DIR}/shttps
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/local/include
        ${CMAKE_CURRENT_SOURCE_DIR}/local/include/openjpeg-2.1
        ${COMMON_LOCAL}/include
        /usr/local/include
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
        src/SipiImage.cpp include/SipiImage.h
        src/formats/SipiIOTiff.cpp include/formats/SipiIOTiff.h
        src/formats/SipiIOJ2k.cpp include/formats/SipiIOJ2k.h
        src/formats/SipiIOJpeg.cpp include/formats/SipiIOJpeg.h
        src/formats/SipiIOPng.cpp include/formats/SipiIOPng.h
        src/formats/SipiIOPdf.cpp include/formats/SipiIOPdf.h
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
        src/formats/SipiIOPdf.cpp include/formats/SipiIOPdf.h
        include/iiifparser/SipiIdentifier.h src/iiifparser/SipiIdentifier.cpp )

add_dependencies(sipi
        icc_profiles)


# !!! ORDER MATTERS !!!
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
        curl
        poppler-cpp
        poppler
        podofo
        idn
        jpeg
        fontconfig
        harfbuzz
        freetype
        bzip2
        unistring
        ${CMAKE_DL_LIBS}
        z
        m)

if(CMAKE_SYSTEM_NAME STREQUAL DARWIN)
	target_link_libraries(sipi
            iconv
            gettext_intl)
else()
    target_link_libraries(sipi rt)
endif()

if(OPENSSL_FOUND)
    target_link_libraries(sipi ${OPENSSL_LIBRARIES})
endif()


install(TARGETS sipi
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/local/bin/lua
        ${CMAKE_CURRENT_SOURCE_DIR}/local/bin/luarocks
#        ${CMAKE_CURRENT_SOURCE_DIR}/local/bin/luarocks-5.3
        DESTINATION ${CMAKE_INSTALL_PREFIX}/bin/
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/config/sipi.config.lua
        DESTINATION ${CMAKE_INSTALL_PREFIX}/etc
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)


#-------------------
# Test
#-------------------

# enables cmake's support for testing
enable_testing()

# custom target to only run 'e2e' tests
add_custom_target(e2e
        DEPENDS sipi
        COMMAND pytest -s --sipi-exec=${CMAKE_BINARY_DIR}/sipi
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test/e2e)

# enables running of 'e2e' tests when `make test` is executed
add_test(NAME all_python_based_e2e_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test/e2e
        COMMAND pytest -s --sipi-exec=$<TARGET_FILE:sipi>)

add_subdirectory(test/unit)
