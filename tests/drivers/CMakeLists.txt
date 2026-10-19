# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_drivers_test drivers_test.cpp)
target_link_libraries(leash_drivers_test PRIVATE leash_drivers Catch2::Catch2WithMain)
add_test(NAME drivers.module COMMAND leash_drivers_test)
set_tests_properties(drivers.module PROPERTIES LABELS "drivers")

add_executable(leash_anthropic_test anthropic_test.cpp)
target_link_libraries(leash_anthropic_test PRIVATE leash_drivers Catch2::Catch2WithMain)
target_compile_definitions(leash_anthropic_test PRIVATE
    LEASH_ANTHROPIC_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/anthropic")
add_test(NAME drivers.anthropic COMMAND leash_anthropic_test)
set_tests_properties(drivers.anthropic PROPERTIES LABELS "drivers")

add_executable(leash_anthropic_live_test anthropic_live_test.cpp)
target_link_libraries(leash_anthropic_live_test PRIVATE leash_drivers Catch2::Catch2WithMain)
add_test(NAME drivers.anthropic_sse COMMAND leash_anthropic_live_test)
set_tests_properties(drivers.anthropic_sse PROPERTIES
    LABELS "network"
    SKIP_RETURN_CODE 4  # Catch2 exits 4 when every test case is skipped
)

add_executable(leash_mock_driver_test mock_test.cpp)
target_link_libraries(leash_mock_driver_test PRIVATE leash_drivers Catch2::Catch2WithMain)
target_compile_definitions(leash_mock_driver_test PRIVATE
    LEASH_MOCK_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/mock")
add_test(NAME drivers.mock COMMAND leash_mock_driver_test)
set_tests_properties(drivers.mock PROPERTIES LABELS "drivers")
