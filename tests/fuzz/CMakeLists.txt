# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
if(NOT LEASH_BUILD_FUZZERS)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "LEASH_BUILD_FUZZERS requires Clang (libFuzzer)")
endif()

add_executable(leash_lexer_fuzz lexer_fuzz.cpp)
add_executable(leash_parser_fuzz parser_fuzz.cpp)

foreach(fuzzer IN ITEMS leash_lexer_fuzz leash_parser_fuzz)
    target_link_libraries(${fuzzer} PRIVATE leash_syntax leash_warnings)
    target_compile_options(${fuzzer} PRIVATE
        -fsanitize=fuzzer,address
        -fno-omit-frame-pointer)
    target_link_options(${fuzzer} PRIVATE
        -fsanitize=fuzzer,address
        -fno-omit-frame-pointer)
endforeach()
