// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
#pragma once

/// Golden-file assertions for the syntax fixtures.
///
/// WP-25 will move this helper into `leash_testkit`; until then it lives here
/// with the same shape: `expect_golden(fixture_path, actual_text)`. The golden
/// file is `<fixture_path>.golden`, so a fixture and its expected diagnostics
/// sit next to each other.
///
/// Set `LEASH_UPDATE_GOLDEN=1` to rewrite goldens (used when adding a fixture,
/// never in CI). On a mismatch the full expected and actual renderings are
/// attached to the Catch2 failure via `INFO`.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace leash::testkit {

[[nodiscard]] inline std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

inline void write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

[[nodiscard]] inline bool updating_goldens() {
    const char* value = std::getenv("LEASH_UPDATE_GOLDEN"); // NOLINT(concurrency-mt-unsafe)
    return value != nullptr && std::string_view{value} == "1";
}

/// Compare `actual` with `<fixture_path>.golden`, or rewrite the golden when
/// `LEASH_UPDATE_GOLDEN=1`.
inline void expect_golden(const std::filesystem::path& fixture_path, std::string_view actual) {
    std::filesystem::path golden_path = fixture_path;
    golden_path += ".golden";

    if (updating_goldens()) {
        write_text_file(golden_path, actual);
        return;
    }

    INFO("golden file: " << golden_path.string());
    REQUIRE(std::filesystem::exists(golden_path));
    const std::string expected = read_text_file(golden_path);
    INFO("--- expected ---\n" << expected << "--- actual ---\n" << actual);
    CHECK(actual == expected);
}

} // namespace leash::testkit
