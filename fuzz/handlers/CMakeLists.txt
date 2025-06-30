# -----------------------
# IIIF Handler fuzz testing
# -----------------------
#
# Fuzzing requires Clang and LLVM.
if (CMAKE_CXX_COMPILER_ID MATCHES "(Apple)?[Cc]lang")
    message(STATUS "Compiling with Clang. Enabling fuzz testing.")
    set(FUZZ_TARGET_NAME iiif_handler_uri_parser_fuzz)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang)

    include_directories(
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/shttps
    )

    set(FUZZ_TARGET_SOURCE_FILES
            ${CMAKE_SOURCE_DIR}/shttps/ChunkReader.cpp
            ${CMAKE_SOURCE_DIR}/shttps/ChunkReader.h
            ${CMAKE_SOURCE_DIR}/shttps/Connection.cpp
            ${CMAKE_SOURCE_DIR}/shttps/Connection.h
            ${CMAKE_SOURCE_DIR}/shttps/Error.cpp
            ${CMAKE_SOURCE_DIR}/shttps/Error.h
            ${CMAKE_SOURCE_DIR}/src/handlers/iiif_handler.cpp
            ${CMAKE_SOURCE_DIR}/src/handlers/iiif_handler.hpp
    )

    add_compile_options(
            # TODO[4] Comment out these coverage options.
            -fsanitize-coverage=trace-cmp
            -fsanitize=fuzzer,address
            -std=c++14
    )

    link_libraries(
            -fsanitize=address
    )

    add_library(lib_${FUZZ_TARGET_NAME} STATIC
            ${FUZZ_TARGET_SOURCE_FILES}
    )

    add_executable(${FUZZ_TARGET_NAME}
            iiif_handler_uri_parser_target.cpp
    )

    target_link_libraries(${FUZZ_TARGET_NAME}
            lib_${FUZZ_TARGET_NAME}
            -fsanitize=fuzzer
            -fuse-ld=lld
    )
endif()
