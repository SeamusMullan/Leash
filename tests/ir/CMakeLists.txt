# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_ir_test ir_test.cpp)
target_link_libraries(leash_ir_test PRIVATE leash_ir Catch2::Catch2WithMain)
add_test(NAME ir.module COMMAND leash_ir_test)
set_tests_properties(ir.module PROPERTIES LABELS "ir")
