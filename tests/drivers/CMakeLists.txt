# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_drivers_test drivers_test.cpp)
target_link_libraries(leash_drivers_test PRIVATE leash_drivers Catch2::Catch2WithMain)
add_test(NAME drivers.module COMMAND leash_drivers_test)
set_tests_properties(drivers.module PROPERTIES LABELS "drivers")
