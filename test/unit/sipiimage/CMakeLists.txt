
link_directories(
        ${CONFIGURE_LIBDIR}
)

include_directories(
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/shttps
        ${COMMON_INCLUDE_FILES_DIR}
        ${COMMON_INCLUDE_FILES_DIR}/metadata
        ${COMMON_INCLUDE_FILES_DIR}/formats
        ${COMMON_INCLUDE_FILES_DIR}/iiifparser
)

file(GLOB SRCS *.cpp)

add_executable(
        sipiimage
        ${SRCS}
        ${PROJECT_SOURCE_DIR}/src/SipiConf.cpp ${PROJECT_SOURCE_DIR}/include/SipiConf.h
        ${PROJECT_SOURCE_DIR}/src/SipiError.cpp ${PROJECT_SOURCE_DIR}/include/SipiError.h
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiIcc.cpp ${PROJECT_SOURCE_DIR}/include/metadata/SipiIcc.h
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiXmp.cpp ${PROJECT_SOURCE_DIR}/include/metadata/SipiXmp.h
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiIptc.cpp ${PROJECT_SOURCE_DIR}/include/metadata/SipiIptc.h
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiExif.cpp ${PROJECT_SOURCE_DIR}/include/metadata/SipiExif.h
        ${PROJECT_SOURCE_DIR}/src/metadata/SipiEssentials.cpp ${PROJECT_SOURCE_DIR}/include/metadata/SipiEssentials.h
        ${PROJECT_SOURCE_DIR}/src/SipiImage.cpp ${PROJECT_SOURCE_DIR}/include/SipiImage.h ${PROJECT_SOURCE_DIR}/include/SipiImageError.h
        ${PROJECT_SOURCE_DIR}/include/SipiIO.h
        ${PROJECT_SOURCE_DIR}/src/formats/SipiIOTiff.cpp ${PROJECT_SOURCE_DIR}/include/formats/SipiIOTiff.h
        ${PROJECT_SOURCE_DIR}/src/formats/SipiIOJ2k.cpp ${PROJECT_SOURCE_DIR}/include/formats/SipiIOJ2k.h
        ${PROJECT_SOURCE_DIR}/src/formats/SipiIOJpeg.cpp ${PROJECT_SOURCE_DIR}/include/formats/SipiIOJpeg.h
        ${PROJECT_SOURCE_DIR}/src/formats/SipiIOPng.cpp ${PROJECT_SOURCE_DIR}/include/formats/SipiIOPng.h
        ${PROJECT_SOURCE_DIR}/src/SipiHttpServer.cpp ${PROJECT_SOURCE_DIR}/include/SipiHttpServer.h
        ${PROJECT_SOURCE_DIR}/src/SipiCache.cpp ${PROJECT_SOURCE_DIR}/include/SipiCache.h
        ${PROJECT_SOURCE_DIR}/src/SipiLua.cpp ${PROJECT_SOURCE_DIR}/include/SipiLua.h
        ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiIdentifier.cpp ${PROJECT_SOURCE_DIR}/include/iiifparser/SipiIdentifier.h
        ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiRotation.cpp ${PROJECT_SOURCE_DIR}/include/iiifparser/SipiRotation.h
        ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiQualityFormat.cpp ${PROJECT_SOURCE_DIR}/include/iiifparser/SipiQualityFormat.h
        ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiRegion.cpp ${PROJECT_SOURCE_DIR}/include/iiifparser/SipiRegion.h
        ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiSize.cpp ${PROJECT_SOURCE_DIR}/include/iiifparser/SipiSize.h
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
        ${PROJECT_SOURCE_DIR}/shttps/makeunique.h ${PROJECT_SOURCE_DIR}/src/SipiFilenameHash.cpp ${PROJECT_SOURCE_DIR}/include/SipiFilenameHash.h
        ../../../include/SipiImageError.h
)
add_dependencies(sipiimage icc_profiles)

target_link_libraries(sipiimage libgtest)

target_link_libraries(sipiimage
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
    target_link_libraries(sipiimage iconv gettext_intl)
else()
    target_link_libraries(sipiimage rt)
endif()

install(TARGETS sipiimage DESTINATION bin)

add_test(NAME sipiimage_unit_test
        COMMAND sipiimage)
