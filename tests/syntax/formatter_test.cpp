// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "leash/syntax/formatter.hpp"
#include "leash/syntax/parser.hpp"

namespace {

using leash::syntax::FileId;
using leash::syntax::Parsed;

[[nodiscard]] Parsed parse(std::string_view source) {
    return leash::syntax::parse_program(source, FileId{1});
}

[[nodiscard]] const leash::syntax::ast::Program& program_of(const Parsed& parsed) {
    if (!parsed.program.has_value()) {
        throw std::runtime_error("parse produced no program");
    }
    return *parsed.program;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace

TEST_CASE("formatter normalises a small program", "[syntax][formatter]") {
    const std::string source = "module a.b\n\nuse c.d\nuse c.e.{F,G}\n\nlet answer:Int=42\n";
    const Parsed parsed = parse(source);
    REQUIRE(parsed.ok());
    const std::string formatted = leash::syntax::format_program(program_of(parsed), source);
    CHECK(formatted == "module a.b\n\nuse c.d\nuse c.e.{F, G}\n\nlet answer: Int = 42\n");
}

TEST_CASE("formatter indents blocks and drops semicolons", "[syntax][formatter]") {
    const std::string source = "flow f(input)->Int{let x=1;\nx}\n";
    const Parsed parsed = parse(source);
    REQUIRE(parsed.ok());
    const std::string formatted = leash::syntax::format_program(program_of(parsed), source);
    CHECK(formatted == "flow f(input) -> Int {\n  let x = 1\n  x\n}\n");
}

TEST_CASE("formatter preserves doc comments and line comments", "[syntax][formatter]") {
    const std::string source = "// header\n"
                               "/// A documented type.\n"
                               "type T = {\n"
                               "  a: Int, // the a\n"
                               "  b: String,\n"
                               "}\n";
    const Parsed parsed = parse(source);
    REQUIRE(parsed.ok());
    const std::string formatted = leash::syntax::format_program(program_of(parsed), source);
    CHECK(formatted.find("// header") != std::string::npos);
    CHECK(formatted.find("/// A documented type.") != std::string::npos);
    CHECK(formatted.find("// the a") != std::string::npos);
    CHECK(formatted.find("type T = {") != std::string::npos);
}

TEST_CASE("formatter is idempotent and round-trips", "[syntax][formatter]") {
    const std::string source =
        "module m\n"
        "\n"
        "type Pair = { left: Int, right: Int }\n"
        "\n"
        "agent helper = Agent { driver: drivers.mock(), system: \"be brief\" }\n"
        "\n"
        "task main {\n"
        "  input { value: Int }\n"
        "  output Int\n"
        "  budget { cost: usd(1), llm_calls: 2 }\n"
        "}\n"
        "\n"
        "flow main(input) -> Int {\n"
        "  let doubled = input.value * 2\n"
        "  if doubled > 10 { fail Model(\"too large\", doubled) }\n"
        "  complete doubled\n"
        "}\n";
    const Parsed first = parse(source);
    REQUIRE(first.ok());
    const std::string once = leash::syntax::format_program(program_of(first), source);

    const Parsed second = parse(once);
    INFO(once);
    REQUIRE(second.ok());
    CHECK(program_of(first) == program_of(second));

    const std::string twice = leash::syntax::format_program(program_of(second), once);
    CHECK(twice == once);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("formatter round-trips every supported example", "[syntax][formatter][examples]") {
    const std::filesystem::path directory = std::filesystem::path{LEASH_SOURCE_DIR} / "examples";
    REQUIRE(std::filesystem::exists(directory));

    std::size_t supported = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".leash") {
            continue;
        }
        const std::string source = read_file(entry.path());
        const Parsed first = parse(source);
        if (!first.ok()) {
            continue;
        }
        ++supported;
        INFO("file: " << entry.path().filename().string());
        const std::string once = leash::syntax::format_program(program_of(first), source);
        const Parsed second = parse(once);
        REQUIRE(second.ok());
        CHECK(program_of(first) == program_of(second));
        const std::string twice = leash::syntax::format_program(program_of(second), once);
        CHECK(twice == once);
        CHECK(once.find("// SPDX-License-Identifier") != std::string::npos);
    }
    CHECK(supported >= 2);
}

TEST_CASE("formatter keeps comments in single-field records", "[syntax][formatter]") {
    const std::string source = "type T = {\n"
                               "  // the only field\n"
                               "  a: Int,\n"
                               "}\n"
                               "flow f(input) {\n"
                               "  let t = T { a: 1 // trailing\n"
                               "  }\n"
                               "  complete t\n"
                               "}\n";
    const Parsed parsed = parse(source);
    REQUIRE(parsed.ok());
    const std::string formatted = leash::syntax::format_program(program_of(parsed), source);
    CHECK(formatted.find("// the only field") != std::string::npos);
    CHECK(formatted.find("// trailing") != std::string::npos);
    CHECK(formatted.find("type T = {") != std::string::npos);
}

TEST_CASE("formatter keeps triple-quoted strings verbatim", "[syntax][formatter]") {
    const std::string source = "flow f(input) {\n"
                               "  let prompt = \"\"\"\n"
                               "    Hello {input.name}\n"
                               "    Bye\n"
                               "  \"\"\"\n"
                               "  complete prompt\n"
                               "}\n";
    const Parsed parsed = parse(source);
    REQUIRE(parsed.ok());
    const std::string formatted = leash::syntax::format_program(program_of(parsed), source);
    CHECK(formatted.find("\"\"\"\n    Hello {input.name}\n    Bye\n  \"\"\"") != std::string::npos);

    const Parsed second = parse(formatted);
    REQUIRE(second.ok());
    CHECK(program_of(parsed) == program_of(second));
}
