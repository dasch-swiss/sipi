#
# Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
# contributors. SPDX-License-Identifier: AGPL-3.0-or-later
#

cmake_minimum_required(VERSION 3.28)
cmake_policy(VERSION 3.18)
if (POLICY CMP0135) # DOWNLOAD_EXTRACT_TIMESTAMP
    cmake_policy(SET CMP0135 OLD)
endif ()

message(STATUS "cmake version: ${CMAKE_VERSION}")

# Require C++23 but let a parent project ask for something higher
if (DEFINED CMAKE_CXX_STANDARD)
    if (CMAKE_CXX_STANDARD MATCHES "^(90|99)$" OR CMAKE_CXX_STANDARD LESS 23)
        message(FATAL_ERROR "This project requires at least C++23. Aborting...")
    endif ()
else ()
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_REQUIRED_FLAGS "-std=c++23") # or the appropriate flag for your compiler
endif ()
# Always enforce the language constraint
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# We need compiler extensions for some of the dependencies
if (NOT DEFINED CMAKE_CXX_EXTENSIONS)
    set(CMAKE_CXX_EXTENSIONS ON)
endif ()

list(APPEND CMAKE_MESSAGE_CONTEXT sipi)
project(sipi
        VERSION 3
        LANGUAGES CXX C
)

set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "Propagate CXX standard to Abseil targets" FORCE)

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

# set cmake default install location
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/local" CACHE PATH "default install path" FORCE)
    message(STATUS CMAKE_INSTALL_DIR: ${CMAKE_INSTALL_PREFIX})
endif ()

set(COMMON_SOURCE_FILES_DIR ${PROJECT_SOURCE_DIR}/src)
set(COMMON_INCLUDE_FILES_DIR ${PROJECT_SOURCE_DIR}/include)
set(COMMON_DOXYGEN ${PROJECT_SOURCE_DIR}/doxygen)
set(COMMON_EXT ${PROJECT_SOURCE_DIR}/ext)
set(COMMON_PATCHES ${PROJECT_SOURCE_DIR}/patches)
set(COMMON_VENDOR ${PROJECT_SOURCE_DIR}/vendor)
set(SIPI_SOURCE_DIR ${PROJECT_SOURCE_DIR})

# Set the build type.  Options are:
#  Debug          : w/ debug symbols, w/o optimization
#  Release        : w/o debug symbols, w/ optimization
#  RelWithDebInfo : w/ debug symbols, w/ optimization
option(CMAKE_BUILD_TYPE "The default build type is Debug" Debug)

#
# Here we determine the compiler and compiler version. We need clang >= 15 or g++ >= 13
#
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
        message(FATAL_ERROR "Requires GCC 13.0 or greater.")
    endif ()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "(Apple)?[Cc]lang")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "15.0")
        message(FATAL_ERROR "Clang version must be 15.0.0 or greater! Aborting...")
    endif ()
else ()
    message(WARNING "You are using an unsupported compiler (${CMAKE_CXX_COMPILER_ID}). Compilation has only been tested with Clang and GCC.")
endif ()


#
# compiler and linker flags configuration
# used to hold the flags for the compiler and linker
# which are then later applied to the different targets
#
add_library(build_config INTERFACE)

#
# Statically compile the C++ standard library into the executable
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    message(STATUS "Compiling with GCC (-static-libstdc++).") # and -lstdc++_libbacktrace.")
    target_link_options(build_config INTERFACE -static-libstdc++) # -lstdc++_libbacktrace)
endif ()

#
# Dynamically compile the C++ standard library into the executable
if (CMAKE_CXX_COMPILER_ID MATCHES "(Apple)?[Cc]lang")
    message(STATUS "Compiling with Clang (-stdlib=libc++).")
    target_link_options(build_config INTERFACE -stdlib=libc++)
    link_libraries(c++abi)
endif ()

#
# Figure out the system we are compiling for and
# set flags for compiling Kakadu and Lua
#
set(DARWIN "Darwin")
set(LINUX "Linux")
math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")
if (CMAKE_SYSTEM_NAME STREQUAL DARWIN)
    set(CMAKE_MACOSX_RPATH 1)
    if (${CMAKE_HOST_SYSTEM_PROCESSOR} MATCHES "x86_64")
        set(KDU_MAKE "Makefile-Mac-x86-${BITS}-gcc")
        set(KDU_ARCH "Mac-x86-${BITS}-gcc")
        set(KDU_EXEC_PLATFORM "Mac-x86-64-gcc")
    else ()
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
message(STATUS "Lua ARCH set as: ${LUA_ARCH}")
message(STATUS "System detected as: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiling Kakadu for: ${KDU_ARCH}")

#
# set compiler flags
#
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(build_config INTERFACE -O0 -g -Wall -Wno-uninitialized -Wno-deprecated -Woverloaded-virtual)
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(build_config INTERFACE -O3 -DNDEBUG -Wall -Wno-uninitialized -Wno-deprecated -Woverloaded-virtual)
endif ()

#
# turn on some features
#
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-fvisibility-inlines-hidden" SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG)
if (SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG)
    target_compile_options(build_config INTERFACE -fvisibility-inlines-hidden)
endif ()
check_cxx_compiler_flag("-fvisibility=hidden" SUPPORTS_FVISIBILITY_FLAG)
if (SUPPORTS_FVISIBILITY_FLAG)
    target_compile_options(build_config INTERFACE -fvisibility=hidden)
endif ()


#
# append path to additional find_package() cmake modules
#
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

#
# Code Coverage
#

# Code Coverage Configuration
add_library(coverage_config INTERFACE)

option(CODE_COVERAGE "Enable coverage reporting" OFF)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    # Add required flags (GCC & LLVM/Clang)
    target_compile_options(coverage_config INTERFACE
            -O0        # no optimization
            -g         # generate debug info
            --coverage # sets all required flags
    )
    target_link_options(coverage_config INTERFACE --coverage)
endif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")

# on macOS, ignore frameworks
# the complete environment should be provided through NIX
#if (CMAKE_SYSTEM_NAME STREQUAL DARWIN)
#    set(CMAKE_FIND_FRAMEWORK NEVER)
#    set(CMAKE_IGNORE_PATH "/usr/local/lib*")
#endif ()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

#
# libmagic
# (used to be build locally from source was moved to prerequisites)
#
find_package(LibMagic REQUIRED)
if (LIBMAGIC_FOUND)
    include_directories(${LibMagic_INCLUDE_DIR})
endif ()

#
# openssl
# (used to be build locally from source was moved to prerequisites)
#
find_package(OpenSSL REQUIRED)
if (OPENSSL_FOUND)
    message(STATUS "OpenSSL dirs: ${OPENSSL_INCLUDE_DIR}")
    message(STATUS "OpenSSL libs: ${OPENSSL_LIBRARIES}")
    include_directories(${OPENSSL_INCLUDE_DIR})
endif ()

#
# libcurl
# (used to be build locally from source was moved to prerequisites)
find_package(CURL REQUIRED)
if (CURL_FOUND)
    message(STATUS "curl dirs: ${CURL_INCLUDE_DIRS}")
    message(STATUS "curl libs: ${CURL_LIBRARIES}")
    include_directories(${CURL_INCLUDE_DIRS})
endif ()

find_package(inih REQUIRED)
if( inih_FOUND )
    message (STATUS "inih_INCLUDE_DIRS : " ${inih_INCLUDE_DIRS} )
    message (STATUS "inih_LIBRARIES : " ${inih_LIBRARIES} )
    message (STATUS "inih_inireader_INCLUDE_DIRS : " ${inih_inireader_INCLUDE_DIRS} )
    message (STATUS "inih_inireader_LIBRARIES : " ${inih_inireader_LIBRARIES} )
endif()


#
# abseil-cpp
# required for opentelemetry-cpp
#
find_package(absl CONFIG REQUIRED)
if (absl_FOUND)
    message(STATUS "abseil found")
endif ()

#
# protobuf
# required for opentelemetry-cpp
# cmake --help-module FindProtobuf
#
find_package(Protobuf CONFIG REQUIRED)
if (Protobuf_FOUND)
    message(STATUS "protobuf dirs: ${Protobuf_INCLUDE_DIRS}")
    message(STATUS "protobuf lib: ${Protobuf_LIBRARIES}")
    message(STATUS "protobuf found")
endif ()

#
# nlohmann_json
# required for opentelemetry-cpp
find_package(nlohmann_json CONFIG REQUIRED)
if (nlohmann_json_FOUND)
    message(STATUS "nlohmann_json found")
endif ()

#
# protobuf
#
find_package(opentelemetry-cpp CONFIG REQUIRED)
if (OPENTELEMETRY_CPP_FOUND)
    message(STATUS "opentelemetry-cpp dirs: ${OPENTELEMETRY_CPP_INCLUDE_DIRS}")
    message(STATUS "opentelemetry-cpp libs: ${OPENTELEMETRY_CPP_LIBRARIES}")
    include_directories(${opentelemetry-cpp_INCLUDE_DIRS})
    message(STATUS "opentelemetry-cpp found")
endif ()



if (NOT WIN32)
    find_library(M_LIBRARY NAMES m)
    if (NOT M_LIBRARY)
        message(STATUS "math lib 'libm' not found; floating point support disabled")
    endif ()
else ()
    # not needed on windows
    set(M_LIBRARY "")
endif ()

set(HAVE_ST_ATIMESPEC 0)
check_struct_has_member("struct stat" st_atimespec sys/stat.h HAVE_ST_ATIMESPEC LANGUAGE CXX)
if (HAVE_ST_ATIMESPEC)
    message(STATUS "Have new struct stat")
else ()
    message(STATUS "Have old struct stat")
endif ()

CHECK_INCLUDE_FILES(malloc.h HAVE_MALLOC_H)

# write out the malloc and atimespec information
configure_file(${PROJECT_SOURCE_DIR}/include/SipiConfig.h.in ${CMAKE_BINARY_DIR}/generated/SipiConfig.h)

#
# "standard" packages that are required for sipi
#
if (CMAKE_SYSTEM_NAME STREQUAL DARWIN)
    find_package(ICONV REQUIRED)
endif ()

#find_package(MATH REQUIRED)

message(STATUS "Adding external dependencies ...")

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/local/lib)

# !!! order is important !!!
# dependent targets need to be added before the target that depends on them
# e.g., png, bzip2 before freetype
include(${COMMON_EXT}/cli11/cli11.cmake)
include(${COMMON_EXT}/sole/sole.cmake)
add_subdirectory(${COMMON_EXT}/xz)
add_subdirectory(${COMMON_EXT}/zlib)
add_subdirectory(${COMMON_EXT}/zstd)
add_subdirectory(${COMMON_EXT}/jpeg)
add_subdirectory(${COMMON_EXT}/jbigkit)
add_subdirectory(${COMMON_EXT}/webp)
add_subdirectory(${COMMON_EXT}/expat)
add_subdirectory(${COMMON_EXT}/png)
add_subdirectory(${COMMON_EXT}/tiff)
add_subdirectory(${COMMON_EXT}/bzip2)
add_subdirectory(${COMMON_EXT}/lcms2)
add_subdirectory(${COMMON_EXT}/exiv2)
add_subdirectory(${COMMON_EXT}/jansson)
add_subdirectory(${COMMON_EXT}/lua)
add_subdirectory(${COMMON_EXT}/luarocks)
add_subdirectory(${COMMON_EXT}/sqlite3)
add_subdirectory(${COMMON_EXT}/kakadu)

message(STATUS "... adding external dependencies done.")

#
# add a target to generate API documentation with Doxygen
#
find_package(Doxygen)
if (DOXYGEN_FOUND)
    add_custom_target(doc
            ${DOXYGEN_EXECUTABLE} ${COMMON_DOXYGEN}/Doxyfile
            WORKING_DIRECTORY ${COMMON_DOXYGEN}
            COMMENT "Generating API documentation with Doxygen" VERBATIM
    )
endif (DOXYGEN_FOUND)

#
# get version from git if not provided externally
#
if (NOT DEFINED EXT_PROVIDED_VERSION AND NOT EXT_PROVIDED_VERSION STREQUAL "OFF")
    message(STATUS "Getting version from git")
    if (EXISTS ${PROJECT_SOURCE_DIR}/.git)
        find_package(Git)
        if (GIT_FOUND)
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
        else (GIT_FOUND)
            SET(BUILD_SCM_REVISION 0)
            SET(BUILD_SCM_TAG 0)
        endif (GIT_FOUND)
    endif (EXISTS ${PROJECT_SOURCE_DIR}/.git)
else ()
    message(STATUS "Using externally provided version")
    SET(BUILD_SCM_REVISION 0)
    SET(BUILD_SCM_TAG ${EXT_PROVIDED_VERSION})
endif (NOT DEFINED EXT_PROVIDED_VERSION AND NOT EXT_PROVIDED_VERSION STREQUAL "OFF")

message(STATUS "Build SCM tag: ${BUILD_SCM_TAG}")
message(STATUS "Build SCM revision: ${BUILD_SCM_REVISION}")

# get current date and time
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M")
message(STATUS "Build timestamp: ${BUILD_TIMESTAMP}")


#
# get version from version.txt
#
file(READ version.txt SIPI_VERSION_STRING)
string(STRIP "${SIPI_VERSION_STRING}" SIPI_VERSION_STRING)
message(STATUS "Sipi version: ${SIPI_VERSION_STRING}")

# write out the version and build information
configure_file(${PROJECT_SOURCE_DIR}/include/SipiVersion.h.in ${CMAKE_BINARY_DIR}/generated/SipiVersion.h)


#
# generate ICC profile includes
#
set(GENERATED_ICC_HEADERS
        ${CMAKE_BINARY_DIR}/generated/AdobeRGB1998_icc.h
        ${CMAKE_BINARY_DIR}/generated/USWebCoatedSWOP_icc.h
        ${CMAKE_BINARY_DIR}/generated/YCC709_icc.h
)

add_custom_command(
        OUTPUT ${GENERATED_ICC_HEADERS}
        COMMENT "Generating ICC profile includes"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/generated
        COMMAND cp ${PROJECT_SOURCE_DIR}/include/ICC-Profiles/AdobeRGB1998.icc ${CMAKE_BINARY_DIR}/generated/AdobeRGB1998.icc
        COMMAND xxd -i AdobeRGB1998.icc AdobeRGB1998_icc.h
        COMMAND rm ${CMAKE_BINARY_DIR}/generated/AdobeRGB1998.icc
        COMMAND cp ${PROJECT_SOURCE_DIR}/include/ICC-Profiles/USWebCoatedSWOP.icc ${CMAKE_BINARY_DIR}/generated/USWebCoatedSWOP.icc
        COMMAND xxd -i USWebCoatedSWOP.icc USWebCoatedSWOP_icc.h
        COMMAND rm ${CMAKE_BINARY_DIR}/generated/USWebCoatedSWOP.icc
        COMMAND cp ${PROJECT_SOURCE_DIR}/include/ICC-Profiles/YCC709.icm ${CMAKE_BINARY_DIR}/generated/YCC709.icm
        COMMAND xxd -i YCC709.icm YCC709_icc.h
        COMMAND rm ${CMAKE_BINARY_DIR}/generated/YCC709.icm
        COMMAND cp ${PROJECT_SOURCE_DIR}/include/ICC-Profiles/Rec709-Rec1886.icc ${CMAKE_BINARY_DIR}/generated/Rec709-Rec1886.icc
        COMMAND xxd -i Rec709-Rec1886.icc Rec709-Rec1886_icc.h
        COMMAND rm ${CMAKE_BINARY_DIR}/generated/Rec709-Rec1886.icc
)

add_custom_target(icc_profiles
        DEPENDS ${GENERATED_ICC_HEADERS}
)

link_directories(${CMAKE_BINARY_DIR}/local/lib)

include_directories(
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/include/iiifparser
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
        ${CMAKE_BINARY_DIR}/local/include
        ${NIX_CMAKE_INCLUDE_PATH}
)

add_executable(${PROJECT_NAME}
        ${PROJECT_SOURCE_DIR}/src/sipi.cpp
        ${PROJECT_SOURCE_DIR}/src/SipiConf.cpp include/SipiConf.h
        ${PROJECT_SOURCE_DIR}/src/SipiError.cpp
        ${PROJECT_SOURCE_DIR}/src/SipiError.hpp
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiIcc.cpp ${PROJECT_SOURCE_DIR}/include/metadata/SipiIcc.h
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiXmp.cpp ${PROJECT_SOURCE_DIR}/include/metadata/SipiXmp.h
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiIptc.cpp ${PROJECT_SOURCE_DIR}/include/metadata/SipiIptc.h
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiExif.cpp ${PROJECT_SOURCE_DIR}/include/metadata/SipiExif.h
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiEssentials.cpp ${PROJECT_SOURCE_DIR}/include/metadata/SipiEssentials.h
        ${PROJECT_SOURCE_DIR}/src/SipiImage.cpp
        ${PROJECT_SOURCE_DIR}/src/SipiImage.hpp
        ${PROJECT_SOURCE_DIR}/src/SipiImageError.hpp
        ${PROJECT_SOURCE_DIR}/src/formats/SipiIOTiff.cpp ${PROJECT_SOURCE_DIR}/include/formats/SipiIOTiff.h
        ${PROJECT_SOURCE_DIR}/src/formats/SipiIOJ2k.cpp ${PROJECT_SOURCE_DIR}/include/formats/SipiIOJ2k.h
        ${PROJECT_SOURCE_DIR}/src/formats/SipiIOJpeg.cpp ${PROJECT_SOURCE_DIR}/include/formats/SipiIOJpeg.h
        ${PROJECT_SOURCE_DIR}/src/formats/SipiIOPng.cpp ${PROJECT_SOURCE_DIR}/include/formats/SipiIOPng.h
        ${PROJECT_SOURCE_DIR}/src/SipiHttpServer.cpp
        ${PROJECT_SOURCE_DIR}/src/SipiHttpServer.hpp
        ${PROJECT_SOURCE_DIR}/src/handlers/iiif_handler.cpp
        ${PROJECT_SOURCE_DIR}/src/handlers/iiif_handler.hpp
        ${PROJECT_SOURCE_DIR}/src/SipiCache.cpp include/SipiCache.h
        ${PROJECT_SOURCE_DIR}/src/SipiLua.cpp include/SipiLua.h
        ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiRotation.cpp include/iiifparser/SipiRotation.h
        ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiQualityFormat.cpp ${PROJECT_SOURCE_DIR}/include/iiifparser/SipiQualityFormat.h
        ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiRegion.cpp ${PROJECT_SOURCE_DIR}/include/iiifparser/SipiRegion.h
        ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiSize.cpp include/iiifparser/SipiSize.h
        ${PROJECT_SOURCE_DIR}/src/SipiCommon.cpp ${PROJECT_SOURCE_DIR}/include/SipiCommon.h
        ${PROJECT_SOURCE_DIR}/shttps/Global.h
        ${PROJECT_SOURCE_DIR}/shttps/Error.cpp ${PROJECT_SOURCE_DIR}/shttps/Error.h
        ${PROJECT_SOURCE_DIR}/shttps/Hash.cpp ${PROJECT_SOURCE_DIR}/shttps/Hash.h
        ${PROJECT_SOURCE_DIR}/shttps/SockStream.cpp ${PROJECT_SOURCE_DIR}/shttps/SockStream.h
        ${PROJECT_SOURCE_DIR}/shttps/ChunkReader.cpp ${PROJECT_SOURCE_DIR}/shttps/ChunkReader.h
        ${PROJECT_SOURCE_DIR}/shttps/Connection.cpp ${PROJECT_SOURCE_DIR}/shttps/Connection.h
        ${PROJECT_SOURCE_DIR}/shttps/LuaServer.cpp ${PROJECT_SOURCE_DIR}/shttps/LuaServer.h
        ${PROJECT_SOURCE_DIR}/shttps/LuaSqlite.cpp ${PROJECT_SOURCE_DIR}/shttps/LuaSqlite.h
        ${PROJECT_SOURCE_DIR}/shttps/Parsing.cpp ${PROJECT_SOURCE_DIR}/shttps/Parsing.h
        ${PROJECT_SOURCE_DIR}/shttps/ThreadControl.cpp ${PROJECT_SOURCE_DIR}/shttps/ThreadControl.h
        ${PROJECT_SOURCE_DIR}/shttps/SocketControl.cpp ${PROJECT_SOURCE_DIR}/shttps/SocketControl.h
        ${PROJECT_SOURCE_DIR}/shttps/Server.cpp ${PROJECT_SOURCE_DIR}/shttps/Server.h
        ${PROJECT_SOURCE_DIR}/shttps/jwt.c ${PROJECT_SOURCE_DIR}/shttps/jwt.h
        ${PROJECT_SOURCE_DIR}/shttps/makeunique.h
        ${PROJECT_SOURCE_DIR}/src/SipiFilenameHash.cpp ${PROJECT_SOURCE_DIR}/include/SipiFilenameHash.h
        ${PROJECT_SOURCE_DIR}/include/iiifparser/SipiIdentifier.h ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiIdentifier.cpp
        ${PROJECT_SOURCE_DIR}/src/handlers/iiif_handler.cpp
        ${PROJECT_SOURCE_DIR}/src/handlers/iiif_handler.hpp
        ${PROJECT_SOURCE_DIR}/src/otel.hpp
)

add_dependencies(${PROJECT_NAME} icc_profiles)

add_dependencies(${PROJECT_NAME} bzip2 zlib xz zstd jpeg jbigkit png webp tiff expat lcms2 exiv2 jansson lua luarocks sqlite3 kdu kdu_aux)

# !!! ORDER MATTERS !!!
# dependent targets need to be added before the target that depends on them
target_link_libraries(${PROJECT_NAME}
        lcms2
        exiv2
        ${inih_inireader_LIBRARIES}
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
        ${CURL_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        jpeg
        bzip2
        zstd
        z
        m
        lzma
        ${CMAKE_DL_LIBS}
)

target_include_directories(${PROJECT_NAME} PRIVATE ${cli11_SOURCE_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE ${sole_SOURCE_DIR})

set(absl_LIBS absl::atomic_hook absl::errno_saver absl::log_severity absl::no_destructor absl::nullability absl::raw_logging_internal absl::spinlock_wait absl::config absl::dynamic_annotations absl::core_headers absl::malloc_internal absl::base_internal absl::base absl::throw_delegate absl::pretty_function absl::endian absl::scoped_set_env absl::strerror absl::fast_type_id absl::prefetch absl::algorithm absl::algorithm_container absl::cleanup_internal absl::cleanup absl::btree absl::compressed_tuple absl::fixed_array absl::inlined_vector_internal absl::inlined_vector absl::flat_hash_map absl::flat_hash_set absl::node_hash_map absl::node_hash_set absl::container_memory absl::hash_function_defaults absl::hash_policy_traits absl::common_policy_traits absl::hashtablez_sampler absl::hashtable_debug absl::hashtable_debug_hooks absl::node_slot_policy absl::raw_hash_map absl::container_common absl::raw_hash_set absl::layout absl::crc_cpu_detect absl::crc_internal absl::crc32c absl::non_temporal_arm_intrinsics absl::non_temporal_memcpy absl::crc_cord_state absl::stacktrace absl::symbolize absl::examine_stack absl::failure_signal_handler absl::debugging_internal absl::demangle_internal absl::leak_check absl::debugging absl::flags_path_util absl::flags_program_name absl::flags_config absl::flags_marshalling absl::flags_commandlineflag_internal absl::flags_commandlineflag absl::flags_private_handle_accessor absl::flags_reflection absl::flags_internal absl::flags absl::flags_usage_internal absl::flags_usage absl::flags_parse absl::any_invocable absl::bind_front absl::function_ref absl::overload absl::hash absl::city absl::low_level_hash absl::log_internal_check_impl absl::log_internal_check_op absl::log_internal_conditions absl::log_internal_config absl::log_internal_flags absl::log_internal_format absl::log_internal_globals absl::log_internal_log_impl absl::log_internal_proto absl::log_internal_message absl::log_internal_log_sink_set absl::log_internal_nullguard absl::log_internal_nullstream absl::log_internal_strip absl::log_internal_voidify absl::log_internal_append_truncated absl::absl_check absl::absl_log absl::check absl::die_if_null absl::log_flags absl::log_globals absl::log_initialize absl::log absl::log_entry absl::log_sink absl::log_sink_registry absl::log_streamer absl::log_internal_structured absl::log_structured absl::vlog_config_internal absl::absl_vlog_is_on absl::vlog_is_on absl::log_internal_fnmatch absl::memory absl::type_traits absl::meta absl::bits absl::int128 absl::numeric absl::numeric_representation absl::sample_recorder absl::exponential_biased absl::periodic_sampler absl::random_random absl::random_bit_gen_ref absl::random_internal_mock_helpers absl::random_distributions absl::random_seed_gen_exception absl::random_seed_sequences absl::random_internal_traits absl::random_internal_distribution_caller absl::random_internal_fast_uniform_bits absl::random_internal_seed_material absl::random_internal_pool_urbg absl::random_internal_salted_seed_seq absl::random_internal_iostream_state_saver absl::random_internal_generate_real absl::random_internal_wide_multiply absl::random_internal_fastmath absl::random_internal_nonsecure_base absl::random_internal_pcg_engine absl::random_internal_randen_engine absl::random_internal_platform absl::random_internal_randen absl::random_internal_randen_slow absl::random_internal_randen_hwaes absl::random_internal_randen_hwaes_impl absl::random_internal_distribution_test_util absl::random_internal_uniform_helper absl::status absl::statusor absl::string_view absl::strings absl::charset absl::has_ostream_operator absl::strings_internal absl::str_format absl::str_format_internal absl::cord_internal absl::cordz_update_tracker absl::cordz_functions absl::cordz_statistics absl::cordz_handle absl::cordz_info absl::cordz_sample_token absl::cordz_update_scope absl::cord absl::graphcycles_internal absl::kernel_timeout_internal absl::synchronization absl::time absl::civil_time absl::time_zone absl::any absl::bad_any_cast absl::bad_any_cast_impl absl::span absl::optional absl::bad_optional_access absl::bad_variant_access absl::variant absl::compare absl::utility absl::if_constexpr)

# add opentelemetry-cpp include and libraries to the target
target_link_libraries(${PROJECT_NAME} ${absl_LIBS} ${Protobuf_LIBRARIES} ${OPENTELEMETRY_CPP_LIBRARIES} Threads::Threads)

# although we are using NIX, we still need to link against the system libraries on macOS,
# thus we need to link against CoreFoundation because the dependencies gotten through NIX
# where built against it :(
if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    find_library(CORE_FOUNDATION_FRAMEWORK CoreFoundation)
    target_link_libraries(${PROJECT_NAME} iconv intl "${CORE_FOUNDATION_FRAMEWORK}")
else ()
    target_link_libraries(${PROJECT_NAME} rt)
endif ()

#
# apply the compiler and linker flags
#
target_link_libraries(${PROJECT_NAME} build_config)
if (CODE_COVERAGE)
    target_link_libraries(${PROJECT_NAME} coverage_config)
endif ()


# install all artifacts

install(TARGETS ${PROJECT_NAME}
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

set_property(TEST all_python_based_e2e_tests PROPERTY LABELS e2e)

#-------------------
# Fuzz testing
#-------------------

# add_subdirectory(fuzz)
