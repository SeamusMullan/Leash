// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include "leash/store/store.hpp"

TEST_CASE("store module reports its name", "[store]") {
    REQUIRE(leash::store::module_name() == "store");
}
