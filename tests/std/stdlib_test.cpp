// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include "leash/std/stdlib.hpp"

TEST_CASE("std module reports its name", "[std]") {
    REQUIRE(leash::stdlib::module_name() == "stdlib");
}
