# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_store_test store_test.cpp)
target_link_libraries(leash_store_test PRIVATE leash_store Catch2::Catch2WithMain)
add_test(NAME store.module COMMAND leash_store_test)
set_tests_properties(store.module PROPERTIES LABELS "store")
