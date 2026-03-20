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
    set(CMAKE_CXX_COMPILER clang++)

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
            ${CMAKE_SOURCE_DIR}/shttps/SockStream.cpp
            ${CMAKE_SOURCE_DIR}/shttps/SockStream.h
            ${CMAKE_SOURCE_DIR}/src/handlers/iiif_handler.cpp
            ${CMAKE_SOURCE_DIR}/src/handlers/iiif_handler.hpp
    )

    add_compile_options(
            # TODO[4] Comment out these coverage options.
            -fsanitize-coverage=trace-cmp
            -fsanitize=fuzzer,address
            -std=c++23
    )

    link_libraries(
            -fsanitize=address
    )

    add_library(lib_${FUZZ_TARGET_NAME} STATIC
            ${FUZZ_TARGET_SOURCE_FILES}
    )
    
    add_dependencies(lib_${FUZZ_TARGET_NAME} lua openssl_ssl openssl_crypto)

    add_executable(${FUZZ_TARGET_NAME}
            iiif_handler_uri_parser_target.cpp
    )

    # Clang on Linux (including Nix) does not implicitly link the C++ stdlib
    # when sanitizer flags override the default link command.
    if (APPLE)
        set(FUZZ_CXX_STDLIB c++)
    else()
        set(FUZZ_CXX_STDLIB stdc++)
    endif()

    target_link_libraries(${FUZZ_TARGET_NAME}
            lib_${FUZZ_TARGET_NAME}
            openssl_ssl
            openssl_crypto
            -fsanitize=fuzzer
            ${FUZZ_CXX_STDLIB}
    )
endif()
