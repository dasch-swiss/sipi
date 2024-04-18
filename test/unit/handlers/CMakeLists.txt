# -----------------------
# Handlers unit tests
# -----------------------
set(TEST_NAME sipi_handlers_tests)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TEST_TARGET_INCLUDE_DIRECTORIES
        ${CMAKE_SOURCE_DIR}/src/handlers
        ${CMAKE_SOURCE_DIR}/shttps
)

set(TEST_TARGET_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/shttps/ChunkReader.cpp
        ${CMAKE_SOURCE_DIR}/shttps/ChunkReader.h
        ${CMAKE_SOURCE_DIR}/shttps/Connection.cpp
        ${CMAKE_SOURCE_DIR}/shttps/Connection.h
        ${CMAKE_SOURCE_DIR}/shttps/Error.cpp
        ${CMAKE_SOURCE_DIR}/shttps/Error.h
        ${CMAKE_SOURCE_DIR}/src/handlers/iiif_handler.cpp
        ${CMAKE_SOURCE_DIR}/src/handlers/iiif_handler.hpp
)

set(SOURCE_FILES
        main.cpp
        iiif_handler_test.cpp
        ${TEST_TARGET_SOURCE_FILES}
)
add_executable(${TEST_NAME})
target_sources(${TEST_NAME} PRIVATE ${SOURCE_FILES})
target_include_directories(${TEST_NAME} PRIVATE ${TEST_TARGET_INCLUDE_DIRECTORIES})

add_dependencies(${TEST_NAME} gtest_main gtest gmock)
target_link_libraries(${TEST_NAME} gtest_main gtest gmock)

target_link_libraries(${TEST_NAME} build_config)
if (WITH_CODE_COVERAGE)
    target_link_libraries(${TEST_NAME} coverage_config)
endif ()

install(TARGETS ${TEST_NAME} DESTINATION bin)
add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
set_property(TEST ${TEST_NAME} PROPERTY LABELS unit)
