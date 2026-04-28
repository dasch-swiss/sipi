set(TEST_NAME sipi_image_tests)

file(GLOB SRCS CONFIGURE_DEPENDS *.cpp)

add_executable(${TEST_NAME} ${SRCS})
target_link_libraries(${TEST_NAME} libgtest libsipi_testable)

install(TARGETS ${TEST_NAME} DESTINATION bin)

add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
set_property(TEST ${TEST_NAME} PROPERTY LABELS unit)

# Kakadu's JPEG2000 encoder crashes (SIGILL) on palette images when compiled
# with Clang on Linux in Debug mode. Skip this specific test until the root
# cause is fixed.
if (CMAKE_CXX_COMPILER_ID MATCHES "[Cc]lang" AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_property(TEST ${TEST_NAME} PROPERTY ENVIRONMENT "GTEST_FILTER=-SipiImage.PALETTE_Conversion")
endif()
