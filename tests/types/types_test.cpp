// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include "leash/types/types.hpp"

TEST_CASE("types module reports its name", "[types]") {
    REQUIRE(leash::types::module_name() == "types");
}
