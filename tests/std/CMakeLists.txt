# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_std_test stdlib_test.cpp)
target_link_libraries(leash_std_test PRIVATE leash_std Catch2::Catch2WithMain)
add_test(NAME std.module COMMAND leash_std_test)
set_tests_properties(std.module PROPERTIES LABELS "std")
