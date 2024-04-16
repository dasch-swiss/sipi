# -----------------------
# Handlers unit tests
# -----------------------
set(TEST_NAME sipi_handlers_tests)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IIIF_HANDLER_SOURCE_FILES
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
        ${IIIF_HANDLER_SOURCE_FILES}
)
add_executable(${TEST_NAME} ${SOURCE_FILES})
add_dependencies(${TEST_NAME} googletest ApprovalTests)

target_include_directories(${TEST_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/shttps)
target_include_directories(${TEST_NAME} PUBLIC ${googletest_SOURCE_DIR}/include/gtest ${googletest_SOURCE_DIR}/include/gmock)
target_link_libraries(${TEST_NAME} googletest)
target_link_libraries(${TEST_NAME} build_config)

if (CODE_COVERAGE)
    target_link_libraries(${TEST_NAME} coverage_config)
endif ()

install(TARGETS ${TEST_NAME} DESTINATION bin)
add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
set_property(TEST ${TEST_NAME} PROPERTY LABELS unit)
