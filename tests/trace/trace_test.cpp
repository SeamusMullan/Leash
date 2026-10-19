// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include "leash/trace/trace.hpp"

TEST_CASE("trace module reports its name", "[trace]") {
    REQUIRE(leash::trace::module_name() == "trace");
}
