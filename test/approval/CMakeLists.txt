# -----------------------
# Google Test Approval Tests Starter
# -----------------------
set(TEST_NAME sipi.approvaltests)
set(CMAKE_CXX_STANDARD 23)

file(GLOB SOURCE_FILES CONFIGURE_DEPENDS *.cpp)

add_executable(${TEST_NAME} ${SOURCE_FILES})
target_link_libraries(${TEST_NAME} ApprovalTests::ApprovalTests libgtest libsipi_testable)
target_compile_definitions(${TEST_NAME} PRIVATE -DAPPROVAL_TESTS_HIDE_DEPRECATED_CODE=1)
add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
set_property(TEST ${TEST_NAME} PROPERTY LABELS approval)
