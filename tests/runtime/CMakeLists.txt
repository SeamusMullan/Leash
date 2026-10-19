# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_runtime_test
    runtime_test.cpp
    program_test.cpp
    value_test.cpp
    env_test.cpp
    eval_test.cpp)
target_link_libraries(leash_runtime_test PRIVATE leash_runtime Catch2::Catch2WithMain)
target_compile_definitions(leash_runtime_test PRIVATE LEASH_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME runtime.module COMMAND leash_runtime_test)
set_tests_properties(runtime.module PROPERTIES LABELS "runtime")
