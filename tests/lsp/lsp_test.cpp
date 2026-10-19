// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include "leash/lsp/lsp.hpp"

TEST_CASE("lsp module reports its name", "[lsp]") { REQUIRE(leash::lsp::module_name() == "lsp"); }
