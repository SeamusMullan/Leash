# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_runtime_test runtime_test.cpp)
target_link_libraries(leash_runtime_test PRIVATE leash_runtime Catch2::Catch2WithMain)
add_test(NAME runtime.module COMMAND leash_runtime_test)
set_tests_properties(runtime.module PROPERTIES LABELS "runtime")
