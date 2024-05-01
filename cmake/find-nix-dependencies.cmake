#
# abseil-cpp
# required for opentelemetry-cpp
# (provided through NIX)
find_package(absl CONFIG REQUIRED)
if (absl_FOUND)
    message(STATUS "abseil found")
endif ()

#
# bzip2
# (provided through NIX)
find_package(BZip2 REQUIRED)

#
# CMath
# (provided through NIX)
find_package(CMath REQUIRED)
if (CMath_FOUND)
    message(STATUS "cmath dirs: ${CMath_INCLUDE_DIRS}")
    message(STATUS "cmath libs: ${CMath_LIBRARIES}")
endif ()

#
# libcurl
# (provided through NIX)
find_package(CURL REQUIRED)
if (CURL_FOUND)
    message(STATUS "curl dirs: ${CURL_INCLUDE_DIRS}")
    message(STATUS "curl libs: ${CURL_LIBRARIES}")
    include_directories(${CURL_INCLUDE_DIRS})
endif ()

#
# deflate
# (provided through NIX)
find_package(Deflate REQUIRED)

#
# Doxygen
#
find_package(Doxygen)
if (DOXYGEN_FOUND)
    add_custom_target(doc
            ${DOXYGEN_EXECUTABLE} ${COMMON_DOXYGEN}/Doxyfile
            WORKING_DIRECTORY ${COMMON_DOXYGEN}
            COMMENT "Generating API documentation with Doxygen" VERBATIM
    )
endif (DOXYGEN_FOUND)

# (provided through NIX)
find_package(exiv2lib REQUIRED CONFIG NAMES exiv2)

#
# expat
# (provided through NIX)
find_package(EXPAT REQUIRED)
if (EXPAT_FOUND)
    message(STATUS "expat dirs: ${EXPAT_INCLUDE_DIRS}")
    message(STATUS "expat libs: ${EXPAT_LIBRARIES}")
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL DARWIN)
    find_package(ICONV REQUIRED)
endif ()

find_package(inih REQUIRED)
if (inih_FOUND)
    message (STATUS "inih_INCLUDE_DIRS : " ${inih_INCLUDE_DIRS} )
    message (STATUS "inih_LIBRARIES : " ${inih_LIBRARIES} )
    message (STATUS "inih_inireader_INCLUDE_DIRS : " ${inih_inireader_INCLUDE_DIRS} )
    message (STATUS "inih_inireader_LIBRARIES : " ${inih_inireader_LIBRARIES} )
endif ()

#
# JBIG
# (provided through NIX)
find_package(JBIG REQUIRED)

#
# libjpeg
# (provided through NIX)
find_package(JPEG REQUIRED)
if (JPEG_FOUND)
    message(STATUS "JPEG dirs: ${JPEG_INCLUDE_DIR}")
    message(STATUS "JPEG libs: ${JPEG_LIBRARIES}")
endif ()

#
# Lerc
# (provided through NIX)
find_package(LERC REQUIRED)

find_package(UNISTRING REQUIRED)
if (UNISTRING_FOUND)
    message(STATUS "libunistring dirs: ${UNISTRING_INCLUDE_DIR}")
    message(STATUS "libunistring libs: ${UNISTRING_LIBRARY}")
    add_library(libunistring::libunistring INTERFACE IMPORTED)
    set_property(TARGET libunistring::libunistring PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${UNISTRING_INCLUDE_DIR}")
    set_property(TARGET libunistring::libunistring PROPERTY INTERFACE_LINK_LIBRARIES "${UNISTRING_LIBRARY}")
endif ()

find_package(Libidn2 REQUIRED)
if (Libidn2_FOUND)
    message(STATUS "libidn2 dirs: ${LIBIDN2_INCLUDE_DIR}")
    message(STATUS "libidn2 libs: ${LIBIDN2_LIBRARIES}")
endif ()

#
# libmagic
# (provided through NIX)
find_package(LibMagic REQUIRED)
if (LIBMAGIC_FOUND)
    include_directories(${LibMagic_INCLUDE_DIR})
endif ()

#
# lzma (new xz codebase)
# (provided through NIX)
find_package(liblzma REQUIRED)
if (liblzma_FOUND)
    message(STATUS "LZMA dirs: ${LIBLZMA_INCLUDE_DIR}")
    message(STATUS "LZMA libs: ${LIBLZMA_LIBRARIES}")
endif ()


find_package(LibPSL REQUIRED)
if (LibPSL_FOUND)
    message(STATUS "libpsl dirs: ${LIBPSL_INCLUDE_DIR}")
    message(STATUS "libpsl libs: ${LIBPSL_LIBRARY}")
    add_library(libpsl::libpsl INTERFACE IMPORTED)
    set_property(TARGET libpsl::libpsl PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${LIBPSL_INCLUDE_DIR}")
    set_property(TARGET libpsl::libpsl PROPERTY INTERFACE_LINK_LIBRARIES "${LIBPSL_LIBRARY}")
endif ()

find_package(LibSSH2 REQUIRED)
if (LibSSH2_FOUND)
    message(STATUS "libssh2 dirs: ${LIBSSH2_INCLUDE_DIR}")
    message(STATUS "libssh2 libs: ${LIBSSH2_LIBRARY}")
    add_library(libssh2::libssh2 INTERFACE IMPORTED)
    set_property(TARGET libssh2::libssh2 PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${LIBSSH2_INCLUDE_DIR}")
    set_property(TARGET libssh2::libssh2 PROPERTY INTERFACE_LINK_LIBRARIES "${LIBSSH2_LIBRARY}")
endif ()

# if found, adds Lua::Lua target
find_package(Lua REQUIRED)
if (LUA_FOUND)
    message(STATUS "Lua dirs: ${LUA_INCLUDE_DIR}")
    message(STATUS "Lua libs: ${LUA_LIBRARIES}")
    add_library(Lua::Lua INTERFACE IMPORTED)
    set_property(TARGET Lua::Lua PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${LUA_INCLUDE_DIR}")
    set_property(TARGET Lua::Lua PROPERTY INTERFACE_LINK_LIBRARIES "${LUA_LIBRARIES}")
endif()

find_package(NGHTTP2 REQUIRED)
if(NGHTTP2_FOUND)
    message(STATUS "nghttp2 dirs: ${NGHTTP2_INCLUDE_DIRS}")
    message(STATUS "nghttp2 libs: ${NGHTTP2_LIBRARIES}")
    add_library(libnghttp2::libnghttp2 INTERFACE IMPORTED)
    set_property(TARGET libnghttp2::libnghttp2 PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${NGHTTP2_INCLUDE_DIRS}")
    set_property(TARGET libnghttp2::libnghttp2 PROPERTY INTERFACE_LINK_LIBRARIES "${NGHTTP2_LIBRARIES}")
endif()


#
# nlohmann_json
# required for opentelemetry-cpp
find_package(nlohmann_json CONFIG REQUIRED)
if (nlohmann_json_FOUND)
    # message(STATUS "nlohmann_json found")
endif ()

#
# openssl
# (provided through NIX)
find_package(OpenSSL REQUIRED)
if (OPENSSL_FOUND)
    message(STATUS "OpenSSL dirs: ${OPENSSL_INCLUDE_DIR}")
    message(STATUS "OpenSSL libs: ${OPENSSL_LIBRARIES}")
    include_directories(${OPENSSL_INCLUDE_DIR})
endif ()

#
# opentelemetry-cpp
# (provided through NIX)
find_package(opentelemetry-cpp CONFIG REQUIRED)
if (OPENTELEMETRY_CPP_FOUND)
    message(STATUS "opentelemetry-cpp dirs: ${OPENTELEMETRY_CPP_INCLUDE_DIRS}")
    message(STATUS "opentelemetry-cpp libs: ${OPENTELEMETRY_CPP_LIBRARIES}")
    include_directories(${opentelemetry-cpp_INCLUDE_DIRS})
endif ()

#
# PNG
# (provided through NIX)
find_package(PNG REQUIRED)

#
# protobuf
# required for opentelemetry-cpp
# cmake --help-module FindProtobuf
# (provided through NIX)
find_package(Protobuf CONFIG REQUIRED)
if (Protobuf_FOUND)
    message(STATUS "protobuf dirs: ${Protobuf_INCLUDE_DIRS}")
    message(STATUS "protobuf lib: ${Protobuf_LIBRARIES}")
endif ()

find_package(SQLite3 REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
if(Threads_FOUND)
    message(STATUS "Threads libs: ${CMAKE_THREAD_LIBS_INIT}")
endif()

#
# libtiff
# (provided through NIX)
find_package(TIFF REQUIRED)
if (TIFF_FOUND)
    message(STATUS "TIFF dirs: ${TIFF_INCLUDE_DIR}")
    message(STATUS "TIFF libs: ${TIFF_LIBRARIES}")
endif ()

#
# libwebp
# (provided through NIX)
find_package(WebP REQUIRED)
if (WebP_FOUND)
    message(STATUS "libwebp_INCLUDE_DIRS : " ${WebP_INCLUDE_DIRS} )
    message(STATUS "libwebp_LIBRARIES : " ${WebP_LIBRARIES} )
endif ()

#
# zlib
# (provided through NIX)
set(ZLIB_USE_STATIC_LIBS "ON")
find_package(ZLIB REQUIRED)
if (ZLIB_FOUND)
    message(STATUS "ZLIB dirs: ${ZLIB_INCLUDE_DIRS}")
    message(STATUS "ZLIB libs: ${ZLIB_LIBRARIES}")
endif ()

#
# zstd
# (provided through NIX)
find_package(ZSTD REQUIRED)
