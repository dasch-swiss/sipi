set(TEST_NAME configuration_tests)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TEST_SOURCE_FILES
        main.cpp
        configuration_tests.cpp
        ${shttps_HEADERS}
        ${shttps_SOURCES}
        ${sipi_HEADERS}
        ${sipi_SOURCES}
)

add_executable(${TEST_NAME})
target_sources(${TEST_NAME} PRIVATE ${TEST_SOURCE_FILES})
target_include_directories(${TEST_NAME} PRIVATE ${sipi_INCLUDE_DIRECTORIES})
target_link_libraries(${TEST_NAME} gtest_main gtest gmock ${sipi_LIBRARIES})

add_dependencies(${TEST_NAME} gtest_main gtest gmock ${sipi_DEPENDENCIES})

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
