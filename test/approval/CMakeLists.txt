# -----------------------
# Google Test Approval Tests Starter
# -----------------------
set(TEST_NAME sipi.approvaltests)
set(CMAKE_CXX_STANDARD 14)
set(SOURCE_FILES
        main.cpp
        googletest_starter_test.cpp
)
add_executable(${TEST_NAME} ${SOURCE_FILES})
target_link_libraries(${TEST_NAME} ApprovalTests::ApprovalTests libgtest)
target_compile_definitions(${TEST_NAME} PRIVATE -DAPPROVAL_TESTS_HIDE_DEPRECATED_CODE=1)
add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
set_property(TEST ${TEST_NAME} PROPERTY LABELS approval)
