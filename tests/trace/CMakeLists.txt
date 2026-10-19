# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_trace_test trace_test.cpp)
target_link_libraries(leash_trace_test PRIVATE leash_trace Catch2::Catch2WithMain)
add_test(NAME trace.module COMMAND leash_trace_test)
set_tests_properties(trace.module PROPERTIES LABELS "trace")
