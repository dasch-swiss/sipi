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


include(ExternalProject)

project(shttps)

option(MAKE_SHARED_SHTTPS "Create shttps using all shared libraries" OFF)


set(DARWIN "Darwin")
set(LINUX "Linux")
if(CMAKE_SYSTEM_NAME STREQUAL DARWIN)
	message(STATUS "Compiling for Darwin...")
	message(STATUS ${CMAKE_SYSTEM_NAME})
    SET(CMAKE_MACOSX_RPATH 1)
    SET(LUA_ARCH "macosx")
    set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
elseif(CMAKE_SYSTEM_NAME STREQUAL LINUX)
	message(STATUS "Compiling for Linux64...")
	message(STATUS ${CMAKE_SYSTEM_NAME})
    SET(LUA_ARCH "linux")
#    set(OPENSSL_ROOT_DIR "/usr/local/Cellar/openssl/1.0.2g")
endif()

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-std=c++11" COMPILER_SUPPORTS_CXX11)
check_cxx_compiler_flag("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif(COMPILER_SUPPORTS_CXX0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
    message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

#dd_definitions(${CMAKE_C_FLAGS} " -g")
add_definitions(${CMAKE_C_FLAGS} " -O3")
set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS}")

add_definitions(${CMAKE_C_FLAGS} " -I${OPENSSL_INCLUDE_DIR}")
#add_definitions(${CMAKE_C_FLAGS} " -DMG_ENABLE_THREADS")

set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
set(CMAKE_MACOSX_RPATH "${CMAKE_INSTALL_PREFIX}/lib")


set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)


#
# get lua
#
ExternalProject_Add(
        project_lua
        INSTALL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/local
        URL http://www.lua.org/ftp/lua-5.3.1.tar.gz
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/lua-5.3.1
        CONFIGURE_COMMAND ls
        BUILD_COMMAND make ${LUA_ARCH}
        INSTALL_COMMAND make install INSTALL_TOP=${CMAKE_CURRENT_SOURCE_DIR}/local
        BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(project_lua install_dir)
add_library(lua STATIC IMPORTED)
set_property(TARGET lua PROPERTY IMPORTED_LOCATION ${install_dir}/lib/liblua${CMAKE_STATIC_LIBRARY_SUFFIX})
add_dependencies(lua project_lua)

#
# get jansson
#
ExternalProject_Add(
        project_jansson
        INSTALL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/local
        GIT_REPOSITORY https://github.com/akheron/jansson.git
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/jansson
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_SOURCE_DIR}/local
)
ExternalProject_Get_Property(project_jansson install_dir)
add_library(jansson STATIC IMPORTED)
set_property(TARGET jansson PROPERTY IMPORTED_LOCATION ${install_dir}/lib/libjansson${CMAKE_STATIC_LIBRARY_SUFFIX})
add_dependencies(jansson project_jansson)

message(STATUS " SERVER-DIR: ${CMAKE_CURRENT_SOURCE_DIR}")
include_directories(AFTER ${CMAKE_CURRENT_SOURCE_DIR}/local/include ${CMAKE_CURRENT_SOURCE_DIR}/spdlog)
add_library(
        shttpobj OBJECT
        Error.cpp Error.h
        SockStream.cpp SockStream.h
        ChunkReader.cpp ChunkReader.h
        Connection.cpp Connection.h
        LuaServer.cpp LuaServer.h
        Parsing.cpp Parsing.h
        Server.cpp Server.h
        jwt.c jwt.h
)

add_library(shttp STATIC $<TARGET_OBJECTS:shttpobj>)
install(TARGETS shttp
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
file(GLOB luaincs "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
install(FILES ${luaincs} DESTINATION include)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/spdlog DESTINATION include)

add_executable(shttp-test Shttp.cpp $<TARGET_OBJECTS:shttpobj>)
target_link_libraries(shttp-test ${LIBS} lua jansson magic z dl pthread)
if(OPENSSL_FOUND)
    target_link_libraries(shttp-test ${OPENSSL_LIBRARIES})
endif()
