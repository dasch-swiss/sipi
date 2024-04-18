
set(TEST_NAME configuration_tests)

set(TEST_TARGET_INCLUDE_DIRECTORIES
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/local/include
)

set(${TEST_TARGET_SOURCE_FILES}
        ${PROJECT_SOURCE_DIR}/shttps/ChunkReader.cpp
        ${PROJECT_SOURCE_DIR}/shttps/Connection.cpp
        ${PROJECT_SOURCE_DIR}/shttps/Error.cpp
        ${PROJECT_SOURCE_DIR}/shttps/Hash.cpp
        ${PROJECT_SOURCE_DIR}/shttps/jwt.c
        ${PROJECT_SOURCE_DIR}/shttps/LuaServer.cpp
        ${PROJECT_SOURCE_DIR}/shttps/LuaSqlite.cpp
        ${PROJECT_SOURCE_DIR}/shttps/Parsing.cpp
        ${PROJECT_SOURCE_DIR}/shttps/Server.cpp
        ${PROJECT_SOURCE_DIR}/shttps/Shttp.cpp
        ${PROJECT_SOURCE_DIR}/shttps/SocketControl.cpp
        ${PROJECT_SOURCE_DIR}/shttps/SockStream.cpp
        ${PROJECT_SOURCE_DIR}/shttps/ThreadControl.cpp

        ${PROJECT_SOURCE_DIR}/src/SipiConf.cpp
        ${PROJECT_SOURCE_DIR}/src/SipiError.cpp
        ${PROJECT_SOURCE_DIR}/src/SipiError.hpp
        ${PROJECT_SOURCE_DIR}/src/SipiFilenameHash.cpp
)
set(SOURCE_FILES
        main.cpp
        configuration_tests.cpp
        ${TEST_TARGET_SOURCE_FILES}
)
add_executable(${TEST_NAME})
target_sources(${TEST_NAME} PRIVATE ${SOURCE_FILES})
target_include_directories(${TEST_NAME} PRIVATE ${TEST_TARGET_INCLUDE_DIRECTORIES})
target_include_directories(${TEST_NAME} PRIVATE ${sole_SOURCE_DIR})

target_link_libraries(${TEST_NAME} gtest_main gtest gmock gmock_main)

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
        bz2_static
        libzstd_static
        zlib
        ${CMAKE_DL_LIBS}
        sole
        cli11
)


if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_link_libraries(${TEST_NAME} iconv intl)
else()
    target_link_libraries(${TEST_NAME} rt)
endif()

target_link_libraries(${TEST_NAME} build_config)
if (WITH_CODE_COVERAGE)
    target_link_libraries(${TEST_NAME} coverage_config)
endif ()

install(TARGETS ${TEST_NAME} DESTINATION bin)
add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

set_property(TEST ${TEST_NAME} PROPERTY LABELS unit)
