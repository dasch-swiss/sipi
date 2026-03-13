set(TEST_NAME filenamehash)

file(GLOB SRCS CONFIGURE_DEPENDS *.cpp)

add_executable(${TEST_NAME} ${SRCS})
target_link_libraries(${TEST_NAME} libgtest libsipi_testable)

install(TARGETS ${TEST_NAME} DESTINATION bin)
add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
set_property(TEST ${TEST_NAME} PROPERTY LABELS unit)
