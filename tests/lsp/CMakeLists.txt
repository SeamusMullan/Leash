# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_lsp_test lsp_test.cpp)
target_link_libraries(leash_lsp_test PRIVATE leash_lsp Catch2::Catch2WithMain)
add_test(NAME lsp.module COMMAND leash_lsp_test)
set_tests_properties(lsp.module PROPERTIES LABELS "lsp")
