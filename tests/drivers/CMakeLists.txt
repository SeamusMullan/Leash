# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_drivers_test drivers_test.cpp)
target_link_libraries(leash_drivers_test PRIVATE leash_drivers Catch2::Catch2WithMain)
add_test(NAME drivers.module COMMAND leash_drivers_test)
set_tests_properties(drivers.module PROPERTIES LABELS "drivers")

add_executable(leash_mock_driver_test mock_test.cpp)
target_link_libraries(leash_mock_driver_test PRIVATE leash_drivers Catch2::Catch2WithMain)
target_compile_definitions(leash_mock_driver_test PRIVATE
    LEASH_MOCK_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/mock")
add_test(NAME drivers.mock COMMAND leash_mock_driver_test)
set_tests_properties(drivers.mock PROPERTIES LABELS "drivers")
