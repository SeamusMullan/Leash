# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_cli_test
    doctor_test.cpp
    exit_test.cpp)
target_link_libraries(leash_cli_test PRIVATE leash_cli_core Catch2::Catch2WithMain)
add_test(NAME cli.unit COMMAND leash_cli_test)
set_tests_properties(cli.unit PROPERTIES LABELS "cli")
