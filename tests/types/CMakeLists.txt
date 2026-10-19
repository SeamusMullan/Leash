# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_types_test types_test.cpp)
target_link_libraries(leash_types_test PRIVATE leash_types Catch2::Catch2WithMain)
add_test(NAME types.module COMMAND leash_types_test)
set_tests_properties(types.module PROPERTIES LABELS "types")
