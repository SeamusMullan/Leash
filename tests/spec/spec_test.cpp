// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include "leash/spec/spec.hpp"

TEST_CASE("spec module reports its name", "[spec]") {
    REQUIRE(leash::spec::module_name() == "spec");
}
