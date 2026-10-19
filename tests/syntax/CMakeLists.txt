# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_syntax_test
    syntax_test.cpp
    lexer_test.cpp
    parser_test.cpp
    formatter_test.cpp
    diagnostic_test.cpp
    ast_visit_test.cpp
    fixture_test.cpp)
target_link_libraries(leash_syntax_test PRIVATE leash_syntax Catch2::Catch2WithMain)
target_include_directories(leash_syntax_test PRIVATE
    ${PROJECT_SOURCE_DIR}/tests)
target_compile_definitions(leash_syntax_test PRIVATE
    LEASH_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    LEASH_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/parse")
add_test(NAME syntax.module COMMAND leash_syntax_test)
set_tests_properties(syntax.module PROPERTIES LABELS "syntax")
