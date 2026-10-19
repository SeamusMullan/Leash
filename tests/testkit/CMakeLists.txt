# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_testkit_test testkit_test.cpp)
target_link_libraries(leash_testkit_test PRIVATE leash_testkit Catch2::Catch2WithMain)
add_test(NAME testkit.module COMMAND leash_testkit_test)
set_tests_properties(testkit.module PROPERTIES LABELS "testkit")
