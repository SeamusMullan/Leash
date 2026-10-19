# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_policy_test policy_test.cpp)
target_link_libraries(leash_policy_test PRIVATE leash_policy Catch2::Catch2WithMain)
add_test(NAME policy.module COMMAND leash_policy_test)
set_tests_properties(policy.module PROPERTIES LABELS "policy")
