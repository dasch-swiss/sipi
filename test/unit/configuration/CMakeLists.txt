include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-fvisibility-inlines-hidden" SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG)
if(SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden -std=c++17")
endif()
check_cxx_compiler_flag("-fvisibility=hidden" SUPPORTS_FVISIBILITY_FLAG)
if(SUPPORTS_FVISIBILITY_FLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -std=c++17")
endif()

set(DARWIN "Darwin")
set(LINUX "Linux")
math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")

#
# this is needed for curl...
#
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    find_library(COREFOUNDATION_FRAMEWORK "CoreFoundation")
    if(NOT COREFOUNDATION_FRAMEWORK)
        message(FATAL_ERROR "CoreFoundation framework not found")
    endif()

    find_library(SYSTEMCONFIGURATION_FRAMEWORK "SystemConfiguration")
    if(NOT SYSTEMCONFIGURATION_FRAMEWORK)
        message(FATAL_ERROR "SystemConfiguration framework not found")
    endif()

    list(APPEND CURL_LIBS "-framework CoreFoundation" "-framework SystemConfiguration")

    if(CMAKE_USE_SECTRANSP)
        find_library(SECURITY_FRAMEWORK "Security")
        if(NOT SECURITY_FRAMEWORK)
            message(FATAL_ERROR "Security framework not found")
        endif()

        set(SSL_ENABLED ON)
        set(USE_SECTRANSP ON)
        list(APPEND CURL_LIBS "-framework Security")
    endif()
endif()


link_directories(
        ${PROJECT_SOURCE_DIR}/local/lib
        ${CONFIGURE_LIBDIR}
        /usr/local/lib
)

include_directories(
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/shttps
        ${PROJECT_SOURCE_DIR}/local/include
        ${COMMON_INCLUDE_FILES_DIR}
        ${COMMON_INCLUDE_FILES_DIR}/metadata
        ${COMMON_INCLUDE_FILES_DIR}/formats
        ${COMMON_INCLUDE_FILES_DIR}/iiifparser
        /usr/local/include
)

file(GLOB SRCS *.cpp)

add_executable(configuration
        ${SRCS}
        ${PROJECT_SOURCE_DIR}/src/SipiConf.cpp ${PROJECT_SOURCE_DIR}/include/SipiConf.h
        ${PROJECT_SOURCE_DIR}/src/SipiError.cpp ${PROJECT_SOURCE_DIR}/include/SipiError.h
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
        ${PROJECT_SOURCE_DIR}/shttps/makeunique.h ${PROJECT_SOURCE_DIR}/src/SipiFilenameHash.cpp ${PROJECT_SOURCE_DIR}/include/SipiFilenameHash.h
)

target_link_libraries(configuration
        libgtest)

target_link_libraries(configuration
        ${LIBS}
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
        ssl
        crypto
        jpeg
        bzip2
        zstd
        z
        m
        lzma
        Threads::Threads
        ${CMAKE_DL_LIBS})


if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_link_libraries(configuration
            iconv
            gettext_intl
            ${COREFOUNDATION_FRAMEWORK}
            ${SYSTEMCONFIGURATION_FRAMEWORK})
else()
    target_link_libraries(configuration rt)
endif()

install(TARGETS configuration DESTINATION bin)


add_test(NAME configuration_unit_test
        COMMAND configuration)
