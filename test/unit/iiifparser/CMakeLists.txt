set(TEST_NAME iiifparser)

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
  ${PROJECT_SOURCE_DIR}/src/SipiError.cpp ${PROJECT_SOURCE_DIR}/src/SipiError.hpp
  ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiSize.cpp ${PROJECT_SOURCE_DIR}/include/iiifparser/SipiSize.h
  ${PROJECT_SOURCE_DIR}/src/iiifparser/SipiRegion.cpp ${PROJECT_SOURCE_DIR}/include/iiifparser/SipiRegion.h
  ${PROJECT_SOURCE_DIR}/shttps/Error.cpp ${PROJECT_SOURCE_DIR}/shttps/Error.h
  ${PROJECT_SOURCE_DIR}/shttps/Parsing.cpp ${PROJECT_SOURCE_DIR}/shttps/Parsing.h
)

target_link_libraries(
  ${TEST_NAME}
  libgtest
  magic
)

target_link_libraries(${TEST_NAME} build_config)
if (CODE_COVERAGE)
    target_link_libraries(${TEST_NAME} coverage_config)
endif ()

install(TARGETS ${TEST_NAME} DESTINATION bin)

add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
set_property(TEST ${TEST_NAME} PROPERTY LABELS unit)
