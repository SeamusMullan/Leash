// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include "leash/syntax/syntax.hpp"

TEST_CASE("syntax module reports its name", "[syntax]") {
    REQUIRE(leash::syntax::module_name() == "syntax");
}
