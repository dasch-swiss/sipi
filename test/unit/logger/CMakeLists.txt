set(TEST_NAME logger)

link_directories(
  ${CMAKE_BINARY_DIR}/local/lib
)

include_directories(
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/include
)

file(GLOB SRCS *.cpp)

add_executable(
  ${TEST_NAME}
  ${SRCS}
  ${PROJECT_SOURCE_DIR}/src/Logger.cpp
  ${PROJECT_SOURCE_DIR}/include/Logger.h
)

target_link_libraries(${TEST_NAME} libgtest jansson)

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

