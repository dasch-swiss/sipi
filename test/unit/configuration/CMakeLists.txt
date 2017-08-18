link_directories(
        /usr/local/lib
        ${PROJECT_SOURCE_DIR}/local/lib
        ${CONFIGURE_LIBDIR}
)

include_directories(
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/shttps
        ${PROJECT_SOURCE_DIR}/local/include
        ${PROJECT_SOURCE_DIR}/local/include/openjpeg-2.1
        ${COMMON_INCLUDE_FILES_DIR}
        ${COMMON_INCLUDE_FILES_DIR}/metadata
        ${COMMON_INCLUDE_FILES_DIR}/formats
        ${COMMON_INCLUDE_FILES_DIR}/iiifparser
        /usr/local/include
)

file(GLOB SRCS *.cpp)

add_executable(
        configuration
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
        ${PROJECT_SOURCE_DIR}/shttps/Server.cpp ${PROJECT_SOURCE_DIR}/shttps/Server.h
        ${PROJECT_SOURCE_DIR}/shttps/jwt.c ${PROJECT_SOURCE_DIR}/shttps/jwt.h
        ${PROJECT_SOURCE_DIR}/shttps/makeunique.h ${PROJECT_SOURCE_DIR}/src/SipiFilenameHash.cpp ${PROJECT_SOURCE_DIR}/include/SipiFilenameHash.h
)

target_link_libraries(
        configuration
        gtest gmock_main)

# MESSAGE(STATUS "LIBS: " ${LIBS})
target_link_libraries(configuration ${LIBS} lcms2 exiv2 expat jpeg tiff jbigkit png kdu_aux kdu xz magic lua jansson sqlite3 dl pthread curl ${CMAKE_DL_LIBS} z m)
if(CMAKE_SYSTEM_NAME STREQUAL DARWIN)
    target_link_libraries(configuration iconv)
else()
    target_link_libraries(configuration rt)
endif()

if(OPENSSL_FOUND)
    target_link_libraries(configuration ${OPENSSL_LIBRARIES})
endif()

install(TARGETS configuration DESTINATION bin)