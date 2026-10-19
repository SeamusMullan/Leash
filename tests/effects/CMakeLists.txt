# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The Leash Authors
add_executable(leash_effects_test effects_test.cpp)
target_link_libraries(leash_effects_test PRIVATE leash_effects Catch2::Catch2WithMain)
add_test(NAME effects.module COMMAND leash_effects_test)
set_tests_properties(effects.module PROPERTIES LABELS "effects")
