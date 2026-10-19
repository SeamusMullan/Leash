// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include "leash/policy/policy.hpp"

TEST_CASE("policy module reports its name", "[policy]") {
    REQUIRE(leash::policy::module_name() == "policy");
}
