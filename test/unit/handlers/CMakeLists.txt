set(TEST_NAME sipi_handlers_tests)

set(SOURCE_FILES
    main.cpp
    iiif_handler_test.cpp
    parse_iiif_uri_corpus_test.cpp
)

add_executable(${TEST_NAME} ${SOURCE_FILES})
target_link_libraries(${TEST_NAME} libgtest libsipi_testable)

# Path to the seed fuzz corpus consumed by parse_iiif_uri_corpus_test.cpp.
target_compile_definitions(${TEST_NAME} PRIVATE
    SIPI_FUZZ_CORPUS_DIR="${CMAKE_SOURCE_DIR}/fuzz/handlers/corpus"
)

install(TARGETS ${TEST_NAME} DESTINATION bin)
add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
set_property(TEST ${TEST_NAME} PROPERTY LABELS unit)
