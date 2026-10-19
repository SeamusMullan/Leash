// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include "leash/testkit/testkit.hpp"

TEST_CASE("testkit module reports its name", "[testkit]") {
    REQUIRE(leash::testkit::module_name() == "testkit");
}
