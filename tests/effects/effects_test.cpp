// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include "leash/effects/effects.hpp"

TEST_CASE("effects module reports its name", "[effects]") {
    REQUIRE(leash::effects::module_name() == "effects");
}
