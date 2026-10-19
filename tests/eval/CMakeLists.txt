# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_eval_test eval_test.cpp)
target_link_libraries(leash_eval_test PRIVATE leash_eval Catch2::Catch2WithMain)
add_test(NAME eval.module COMMAND leash_eval_test)
set_tests_properties(eval.module PROPERTIES LABELS "eval")
