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

# Pin the ICC profile creation date so JPEG/PNG/JP2 outputs are byte-exact
# under ctest. SipiIcc::iccBytes() reads SOURCE_DATE_EPOCH and overwrites
# bytes 24-35 of the serialized profile when set; without it lcms2 stamps
# a wall-clock time and the goldens drift by one byte per run. CMake's
# ENVIRONMENT property replaces (does not append to) the parent env.
# Production never sets the var, so behaviour outside ctest is unchanged.
# 946684800 = 2000-01-01T00:00:00Z UTC (arbitrary SIPI-test convention).
set_tests_properties(${TEST_NAME} PROPERTIES
    ENVIRONMENT "SOURCE_DATE_EPOCH=946684800")
