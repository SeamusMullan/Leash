# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_spec_test spec_test.cpp)
target_link_libraries(leash_spec_test PRIVATE leash_spec Catch2::Catch2WithMain)
target_compile_definitions(leash_spec_test PRIVATE
    LEASH_SPEC_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/spec/fixtures")
add_test(NAME spec.module COMMAND leash_spec_test)
set_tests_properties(spec.module PROPERTIES LABELS "spec")
