set(TEST_NAME sipi_image_tests)

set(TEST_TARGET_INCLUDE_DIRECTORIES
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/local/include
        ${CMAKE_BINARY_DIR}/generated
)

set(${TEST_TARGET_SOURCE_FILES}
        ${PROJECT_SOURCE_DIR}/src/SipiConf.cpp
        ${PROJECT_SOURCE_DIR}/src/SipiError.cpp
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiIcc.cpp
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiXmp.cpp
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiIptc.cpp
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiExif.cpp
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiEssentials.cpp
        ${PROJECT_SOURCE_DIR}/src/SipiImage.cpp
        ${PROJECT_SOURCE_DIR}/src/formats/SipiIOTiff.cpp
        ${PROJECT_SOURCE_DIR}/src/formats/SipiIOJ2k.cpp
        ${PROJECT_SOURCE_DIR}/src/formats/SipiIOJpeg.cpp
        ${PROJECT_SOURCE_DIR}/src/formats/SipiIOPng.cpp
        ${PROJECT_SOURCE_DIR}/src/SipiHttpServer.cpp
        ${PROJECT_SOURCE_DIR}/src/handlers/iiif_handler.cpp
        ${PROJECT_SOURCE_DIR}/src/SipiCache.cpp
        ${PROJECT_SOURCE_DIR}/src/SipiLua.cpp
        ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiIdentifier.cpp
        ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiRotation.cpp
        ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiQualityFormat.cpp
        ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiRegion.cpp
        ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiSize.cpp
        ${PROJECT_SOURCE_DIR}/src/SipiCommon.cpp
        ${PROJECT_SOURCE_DIR}/src/SipiFilenameHash.cpp
        ${PROJECT_SOURCE_DIR}/shttps/Error.cpp
        ${PROJECT_SOURCE_DIR}/shttps/Hash.cpp
        ${PROJECT_SOURCE_DIR}/shttps/SockStream.cpp
        ${PROJECT_SOURCE_DIR}/shttps/ChunkReader.cpp
        ${PROJECT_SOURCE_DIR}/shttps/Connection.cpp
        ${PROJECT_SOURCE_DIR}/shttps/LuaServer.cpp
        ${PROJECT_SOURCE_DIR}/shttps/LuaSqlite.cpp
        ${PROJECT_SOURCE_DIR}/shttps/Parsing.cpp
        ${PROJECT_SOURCE_DIR}/shttps/ThreadControl.cpp
        ${PROJECT_SOURCE_DIR}/shttps/SocketControl.cpp
        ${PROJECT_SOURCE_DIR}/shttps/Server.cpp
        ${PROJECT_SOURCE_DIR}/shttps/jwt.c
)

set(SOURCE_FILES
        main.cpp
        sipi_image_tests.cpp
        ${TEST_TARGET_SOURCE_FILES}
)

add_executable(${TEST_NAME})

add_dependencies(${TEST_NAME} icc_profiles)
add_dependencies(${TEST_NAME} zlib xz bz2_static libzstd_static jpeg jbigkit png_static webp tiff expat lcms2 exiv2 jansson lua luarocks sqlite3 kdu kdu_aux sole cli11)

target_sources(${TEST_NAME} PRIVATE ${SOURCE_FILES})
target_include_directories(${TEST_NAME} PRIVATE ${TEST_TARGET_INCLUDE_DIRECTORIES})

target_link_libraries(${TEST_NAME} gtest_main gtest gmock)

target_link_libraries(${TEST_NAME}
        lcms2
        exiv2
        expat
        tiff
        webp
        jbigkit
        png_static
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
        m
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
