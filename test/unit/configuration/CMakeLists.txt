
set(TEST_NAME configuration_tests)

link_directories(
        ${CMAKE_BINARY_DIR}/local/lib
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

add_executable(${TEST_NAME}
        ${SRCS}
        ${PROJECT_SOURCE_DIR}/src/SipiConf.cpp ${PROJECT_SOURCE_DIR}/include/SipiConf.h
        ${PROJECT_SOURCE_DIR}/src/Logger.cpp ${PROJECT_SOURCE_DIR}/include/Logger.h
        ${PROJECT_SOURCE_DIR}/src/SipiError.cpp
        ${PROJECT_SOURCE_DIR}/src/SipiError.hpp
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

add_dependencies(${TEST_NAME} lua)

target_link_libraries(${TEST_NAME}
        libgtest)

target_link_libraries(${TEST_NAME}
        ${LIBS}
        lcms2
        exiv2
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
    target_link_libraries(${TEST_NAME} iconv intl)
else()
    target_link_libraries(${TEST_NAME} rt)
endif()

target_link_libraries(${TEST_NAME} build_config)
if (CODE_COVERAGE)
    target_link_libraries(${TEST_NAME} coverage_config)
endif ()

install(TARGETS ${TEST_NAME} DESTINATION bin)
add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

set_property(TEST ${TEST_NAME} PROPERTY LABELS unit)
