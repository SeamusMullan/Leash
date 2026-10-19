// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "leash/syntax/diagnostic.hpp"
#include "leash/syntax/formatter.hpp"
#include "leash/syntax/parser.hpp"
#include "support/golden.hpp"

namespace {

using leash::syntax::FileId;
using leash::syntax::Parsed;
using leash::testkit::expect_golden;
using leash::testkit::read_text_file;

[[nodiscard]] std::filesystem::path fixture_root() {
    return std::filesystem::path{LEASH_FIXTURE_DIR};
}

[[nodiscard]] std::vector<std::filesystem::path>
leash_files(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    REQUIRE(std::filesystem::exists(directory));
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".leash") {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    return files;
}

[[nodiscard]] Parsed parse(std::string_view source) {
    return leash::syntax::parse_program(source, FileId{1});
}

[[nodiscard]] const leash::syntax::ast::Program& program_of(const Parsed& parsed) {
    if (!parsed.program.has_value()) {
        throw std::runtime_error("parse produced no program");
    }
    return *parsed.program;
}

[[nodiscard]] std::string render_all(const Parsed& parsed, std::string_view source,
                                     const std::filesystem::path& fixture) {
    std::string rendered;
    for (const leash::syntax::Diagnostic& diagnostic : parsed.diagnostics) {
        rendered +=
            leash::syntax::render_to_string(diagnostic, source, fixture.filename().string());
    }
    return rendered;
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity): fixture loop and asserts
TEST_CASE("every good fixture parses, formats and round-trips", "[syntax][fixtures][good]") {
    const std::vector<std::filesystem::path> files = leash_files(fixture_root() / "good");
    REQUIRE(files.size() >= 12);

    for (const std::filesystem::path& file : files) {
        INFO("fixture: " << file.filename().string());
        const std::string source = read_text_file(file);
        const Parsed first = parse(source);
        REQUIRE(first.ok());

        const std::string once = leash::syntax::format_program(program_of(first), source);
        const Parsed second = parse(once);
        INFO("formatted output:\n" << once);
        REQUIRE(second.ok());
        CHECK(program_of(first) == program_of(second));

        const std::string twice = leash::syntax::format_program(program_of(second), once);
        CHECK(twice == once);
    }
}

TEST_CASE("every bad fixture matches its rendered-diagnostic golden", "[syntax][fixtures][bad]") {
    const std::vector<std::filesystem::path> files = leash_files(fixture_root() / "bad");
    REQUIRE(files.size() >= 8);

    for (const std::filesystem::path& file : files) {
        INFO("fixture: " << file.filename().string());
        const std::string source = read_text_file(file);
        const Parsed parsed = parse(source);
        REQUIRE_FALSE(parsed.diagnostics.empty());
        expect_golden(file, render_all(parsed, source, file));
    }
}

TEST_CASE("recovery produces one diagnostic per independent error",
          "[syntax][fixtures][recovery]") {
    const std::filesystem::path file = fixture_root() / "bad" / "01_two_independent_errors.leash";
    const std::string source = read_text_file(file);
    const Parsed parsed = parse(source);
    CHECK(parsed.diagnostics.size() >= 2);
}

TEST_CASE("examples are recorded as good or unsupported fixtures", "[syntax][fixtures][examples]") {
    const std::filesystem::path examples = std::filesystem::path{LEASH_SOURCE_DIR} / "examples";
    const std::filesystem::path golden_dir = fixture_root() / "examples";

    const std::string hello_source = read_text_file(examples / "01_hello.leash");
    CHECK(parse(hello_source).ok());

    struct UnsupportedExample {
        const char* file;
        const char* construct;
    };
    const std::vector<UnsupportedExample> unsupported = {
        {.file = "02_fix_bug.leash", .construct = "require"},
        {.file = "03_triage_parallel.leash", .construct = "enum"},
        {.file = "05_mcp_tools.leash", .construct = "tool"},
    };

    for (const UnsupportedExample& example : unsupported) {
        INFO("example: " << example.file);
        const std::filesystem::path path = examples / example.file;
        const std::string source = read_text_file(path);
        const Parsed parsed = parse(source);
        REQUIRE_FALSE(parsed.diagnostics.empty());
        REQUIRE(parsed.diagnostics.front().code == "E0100");
        CHECK(parsed.diagnostics.front().message.find(example.construct) != std::string::npos);

        std::filesystem::path golden_fixture = golden_dir / example.file;
        expect_golden(golden_fixture, render_all(parsed, source, path));
    }
}

TEST_CASE("example 04 stays inside the v0 subset", "[syntax][fixtures][examples]") {
    // WP-03's acceptance text lists 02..05 as unsupported, but
    // examples/04_review_debate.leash only uses constructs in the v0 subset:
    // `debate` is an ordinary call, not a language construct. Record the real
    // behaviour instead of inventing a diagnostic for it.
    const std::filesystem::path path =
        std::filesystem::path{LEASH_SOURCE_DIR} / "examples" / "04_review_debate.leash";
    const std::string source = read_text_file(path);
    const Parsed parsed = parse(source);
    INFO(render_all(parsed, source, path));
    CHECK(parsed.ok());
}
