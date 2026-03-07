set(TEST_NAME cache_tests)

link_directories(
  ${CMAKE_BINARY_DIR}/local/lib
)

include_directories(
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/shttps
  ${COMMON_INCLUDE_FILES_DIR}
)

file(GLOB SRCS *.cpp)

add_executable(${TEST_NAME}
  ${SRCS}
  ${PROJECT_SOURCE_DIR}/src/SipiCache.cpp ${PROJECT_SOURCE_DIR}/include/SipiCache.h
  ${PROJECT_SOURCE_DIR}/src/SipiError.cpp ${PROJECT_SOURCE_DIR}/src/SipiError.hpp
  ${PROJECT_SOURCE_DIR}/src/Logger.cpp ${PROJECT_SOURCE_DIR}/include/Logger.h
  ${PROJECT_SOURCE_DIR}/shttps/Error.cpp ${PROJECT_SOURCE_DIR}/shttps/Error.h
  ${PROJECT_SOURCE_DIR}/shttps/Global.h
)

target_link_libraries(${TEST_NAME} libgtest jansson)
target_link_libraries(${TEST_NAME} ${SIPI_RUNTIME_SYSTEM_LIBS})

target_link_libraries(${TEST_NAME} build_config)
if (CODE_COVERAGE)
    target_link_libraries(${TEST_NAME} coverage_config)
endif ()

install(TARGETS ${TEST_NAME} DESTINATION bin)
add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
set_property(TEST ${TEST_NAME} PROPERTY LABELS unit)
