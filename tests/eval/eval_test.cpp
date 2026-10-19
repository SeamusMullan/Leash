// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include "leash/eval/eval.hpp"

TEST_CASE("eval module reports its name", "[eval]") {
    REQUIRE(leash::eval::module_name() == "eval");
}
