// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
#include <catch2/catch_test_macros.hpp>

#include "leash/cli/exit.hpp"

TEST_CASE("exit codes match docs/10", "[cli]") {
    using leash::cli::ExitCode;
    using leash::cli::to_exit_code;

    REQUIRE(to_exit_code(ExitCode::ok) == 0);
    REQUIRE(to_exit_code(ExitCode::failure) == 1);
    REQUIRE(to_exit_code(ExitCode::usage) == 2);
    REQUIRE(to_exit_code(ExitCode::replay_divergence) == 3);
    REQUIRE(to_exit_code(ExitCode::budget_or_permission) == 4);
    REQUIRE(to_exit_code(ExitCode::interrupted) == 130);
}
