// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include "leash/drivers/drivers.hpp"

TEST_CASE("drivers module reports its name", "[drivers]") {
    REQUIRE(leash::drivers::module_name() == "drivers");
}
