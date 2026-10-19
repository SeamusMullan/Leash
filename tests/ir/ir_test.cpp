// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include "leash/ir/ir.hpp"

TEST_CASE("ir module reports its name", "[ir]") { REQUIRE(leash::ir::module_name() == "ir"); }
