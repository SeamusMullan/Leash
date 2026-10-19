// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "leash/cli/doctor.hpp"

TEST_CASE("doctor table renders one row per probe", "[cli]") {
    leash::cli::DoctorReport report;
    report.probes = {
        {.name = "hermes", .found = true, .detail = "hermes 2.0.0"},
        {.name = "claude", .found = false, .detail = "not found on PATH"},
    };

    const std::string text = report.render();
    REQUIRE(text.find("hermes") != std::string::npos);
    REQUIRE(text.find("ok") != std::string::npos);
    REQUIRE(text.find("claude") != std::string::npos);
    REQUIRE(text.find("missing") != std::string::npos);
}
