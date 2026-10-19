// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "leash/spec/journal.hpp"
#include "leash/spec/lir.hpp"
#include "leash/spec/manifest.hpp"
#include "leash/spec/money.hpp"
#include "leash/spec/prices.hpp"
#include "leash/spec/profile.hpp"
#include "leash/spec/spec.hpp"
#include "leash/spec/traces.hpp"
#include "leash/spec/usage.hpp"

namespace {

constexpr std::string_view kFixtureDir = LEASH_SPEC_FIXTURE_DIR;

std::filesystem::path fixture(std::string_view relative) {
    return std::filesystem::path(kFixtureDir) / relative;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path);
    REQUIRE(input.good());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::vector<std::string> lines;
    std::ifstream input(path);
    REQUIRE(input.good());
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

struct TraceCase {
    leash::spec::traces::TraceKind kind;
    std::string_view file;
};

constexpr auto kTraceCases = std::to_array<TraceCase>({
    {.kind = leash::spec::traces::TraceKind::action, .file = "traces/action.jsonl"},
    {.kind = leash::spec::traces::TraceKind::tool, .file = "traces/tool.jsonl"},
    {.kind = leash::spec::traces::TraceKind::context, .file = "traces/context.jsonl"},
    {.kind = leash::spec::traces::TraceKind::verification, .file = "traces/verification.jsonl"},
    {.kind = leash::spec::traces::TraceKind::attribution, .file = "traces/attribution.jsonl"},
    {.kind = leash::spec::traces::TraceKind::intervention, .file = "traces/intervention.jsonl"},
    {.kind = leash::spec::traces::TraceKind::entropy, .file = "traces/entropy.jsonl"},
    {.kind = leash::spec::traces::TraceKind::outcome, .file = "traces/outcome.jsonl"},
});

} // namespace

TEST_CASE("spec module reports its name", "[spec]") {
    REQUIRE(leash::spec::module_name() == "spec");
}

TEST_CASE("every hello journal line validates", "[spec][journal]") {
    const auto lines = read_lines(fixture("journal/hello.jsonl"));
    REQUIRE(lines.size() == 6);
    for (const auto& line : lines) {
        const auto result = leash::spec::journal::validate_line(line);
        INFO(line);
        REQUIRE(result.has_value());
    }
}

TEST_CASE("an invalid journal line reports the offending pointer", "[spec][journal]") {
    const auto lines = read_lines(fixture("journal/invalid.jsonl"));
    REQUIRE(lines.size() == 1);

    const auto result = leash::spec::journal::validate_line(lines.front());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(std::holds_alternative<leash::spec::SchemaError>(result.error()));

    const auto& error = std::get<leash::spec::SchemaError>(result.error());
    REQUIRE(error.json_pointer == "/status");
}

TEST_CASE("malformed journal JSON is a ParseError", "[spec][journal]") {
    const auto result = leash::spec::journal::validate_line("{not json");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(std::holds_alternative<leash::spec::ParseError>(result.error()));
}

TEST_CASE("a malformed CAS payload is rejected at the payload pointer", "[spec][journal]") {
    const auto lines = read_lines(fixture("journal/hello.jsonl"));
    auto value = nlohmann::json::parse(lines.at(2));
    value.at("input") = nlohmann::json{{"cas", "not-a-hash"}};

    const auto result = leash::spec::journal::validate_line(value.dump());
    REQUIRE_FALSE(result.has_value());
    const auto& error = std::get<leash::spec::SchemaError>(result.error());
    REQUIRE(error.json_pointer == "/input/cas");
}

TEST_CASE("the manifest fixture validates", "[spec][manifest]") {
    const auto value = nlohmann::json::parse(read_text(fixture("manifest.json")));
    REQUIRE(leash::spec::manifest::validate(value).has_value());
}

TEST_CASE("a manifest with the wrong journal version fails", "[spec][manifest]") {
    auto value = nlohmann::json::parse(read_text(fixture("manifest.json")));
    value.at("journal_version") = 2;

    const auto result = leash::spec::manifest::validate(value);
    REQUIRE_FALSE(result.has_value());
    const auto& error = std::get<leash::spec::SchemaError>(result.error());
    REQUIRE(error.json_pointer == "/journal_version");
}

TEST_CASE("the LIR fixture validates", "[spec][lir]") {
    const auto value = nlohmann::json::parse(read_text(fixture("lir.json")));
    REQUIRE(leash::spec::lir::validate(value).has_value());
}

TEST_CASE("an effectful LIR node needs a site id", "[spec][lir]") {
    auto value = nlohmann::json::parse(read_text(fixture("lir.json")));
    value.at("items").at(1).erase("site_id");

    const auto result = leash::spec::lir::validate(value);
    REQUIRE_FALSE(result.has_value());
    const auto& error = std::get<leash::spec::SchemaError>(result.error());
    REQUIRE(error.json_pointer == "/items/1/site_id");
}

TEST_CASE("each trace fixture validates against its own schema", "[spec][traces]") {
    for (const auto& trace : kTraceCases) {
        const auto lines = read_lines(fixture(trace.file));
        REQUIRE(lines.size() == 1);
        const auto value = nlohmann::json::parse(lines.front());
        INFO(trace.file);
        REQUIRE(leash::spec::traces::validate(trace.kind, value).has_value());
    }
}

TEST_CASE("the outcome JSON fixture validates", "[spec][traces]") {
    const auto value = nlohmann::json::parse(read_text(fixture("traces/outcome.json")));
    REQUIRE(
        leash::spec::traces::validate(leash::spec::traces::TraceKind::outcome, value).has_value());
}

TEST_CASE("a trace fixture fails against every other schema", "[spec][traces]") {
    for (const auto& trace : kTraceCases) {
        const auto lines = read_lines(fixture(trace.file));
        const auto value = nlohmann::json::parse(lines.front());
        for (const auto& other : kTraceCases) {
            if (other.kind == trace.kind) {
                continue;
            }
            INFO(trace.file);
            INFO(other.file);
            const auto result = leash::spec::traces::validate(other.kind, value);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(std::holds_alternative<leash::spec::SchemaError>(result.error()));
        }
    }
}

TEST_CASE("h3 exposes all eleven harness artifacts", "[spec][profiles]") {
    const auto profile = leash::spec::profiles::load("h3");
    REQUIRE(profile.has_value());
    REQUIRE(profile->artifacts.size() == 6);
    REQUIRE(profile->protocols.size() == 5);
    REQUIRE(leash::spec::profiles::exposed_count(*profile) == 11);
    REQUIRE(profile->artifacts.contains(leash::spec::profiles::ArtifactKey::check_registry));
    REQUIRE(profile->protocols.front() == leash::spec::profiles::ProtocolKey::tool_usage);
}

TEST_CASE("h0 exposes nothing", "[spec][profiles]") {
    const auto profile = leash::spec::profiles::load("h0");
    REQUIRE(profile.has_value());
    REQUIRE(profile->artifacts.empty());
    REQUIRE(profile->protocols.empty());
    REQUIRE(leash::spec::profiles::exposed_count(*profile) == 0);
}

TEST_CASE("h1 and h2 expose their ladder rungs", "[spec][profiles]") {
    const auto h1_profile = leash::spec::profiles::load("h1");
    REQUIRE(h1_profile.has_value());
    REQUIRE(leash::spec::profiles::exposed_count(*h1_profile) == 3);

    const auto h2_profile = leash::spec::profiles::load("h2");
    REQUIRE(h2_profile.has_value());
    REQUIRE(leash::spec::profiles::exposed_count(*h2_profile) == 6);
    REQUIRE(h2_profile->artifacts.contains(leash::spec::profiles::ArtifactKey::memory));
    REQUIRE(h2_profile->artifacts.contains(leash::spec::profiles::ArtifactKey::task_state));
}

TEST_CASE("an unknown profile is reported by name", "[spec][profiles]") {
    const auto profile = leash::spec::profiles::load("does-not-exist");
    REQUIRE_FALSE(profile.has_value());
    REQUIRE(std::holds_alternative<leash::spec::UnknownProfileError>(profile.error()));
    REQUIRE(std::get<leash::spec::UnknownProfileError>(profile.error()).name == "does-not-exist");
}

TEST_CASE("the embedded price table lists every shipped model", "[spec][prices]") {
    const auto table = leash::spec::prices::PriceTable::default_embedded();
    constexpr std::array<std::string_view, 9> kModels{
        "claude-opus-5",    "claude-opus-4-8",  "claude-opus-4-7",
        "claude-opus-4-6",  "claude-sonnet-5",  "claude-sonnet-4-6",
        "claude-haiku-4-5", "claude-fable-5-1", "claude-fable-5",
    };
    for (const auto model : kModels) {
        INFO(model);
        REQUIRE(table.price_for(model).has_value());
    }
}

TEST_CASE("opus costs seven fifty for one million in and one hundred thousand out",
          "[spec][prices]") {
    const auto table = leash::spec::prices::PriceTable::default_embedded();

    leash::spec::Usage usage;
    usage.input_tokens = 1'000'000;
    usage.output_tokens = 100'000;

    const auto cost = table.cost_usd("claude-opus-5", usage);
    REQUIRE(cost == leash::spec::Money::usd(7.5));
    REQUIRE(cost.value_or(leash::spec::Money::micros(0)).display() == "$7.5000");
}

TEST_CASE("an unknown model has no price and no cost", "[spec][prices]") {
    const auto table = leash::spec::prices::PriceTable::default_embedded();
    REQUIRE_FALSE(table.price_for("unknown-model").has_value());

    leash::spec::Usage usage;
    usage.input_tokens = 1'000'000;
    REQUIRE_FALSE(table.cost_usd("unknown-model", usage).has_value());
}

TEST_CASE("cache reads use the per-model price", "[spec][prices]") {
    const auto table = leash::spec::prices::PriceTable::default_embedded();

    leash::spec::Usage usage;
    usage.cache_read_input_tokens = 1'000'000;
    const auto fable = table.cost_usd("claude-fable-5-1", usage);
    REQUIRE(fable == leash::spec::Money::usd(0.25));

    const auto opus = table.cost_usd("claude-opus-5", usage);
    REQUIRE(opus == leash::spec::Money::usd(0.5));
}

TEST_CASE("overrides merge over the embedded table", "[spec][prices]") {
    const auto table = leash::spec::prices::PriceTable::default_embedded();
    const auto overridden = table.with_overrides(R"(
[models."claude-opus-5"]
output = 30.0
)");
    REQUIRE(overridden.has_value());

    leash::spec::Usage usage;
    usage.input_tokens = 1'000'000;
    usage.output_tokens = 100'000;
    const auto cost = overridden->cost_usd("claude-opus-5", usage);
    REQUIRE(cost == leash::spec::Money::usd(8.0));

    // The original table is untouched.
    REQUIRE(table.cost_usd("claude-opus-5", usage) == leash::spec::Money::usd(7.5));
}

TEST_CASE("money subtraction never crosses zero", "[spec][money]") {
    const auto three = leash::spec::Money::usd(3.12);
    REQUIRE(three.display() == "$3.1200");

    const auto remainder = three.checked_sub(leash::spec::Money::usd(1.12));
    REQUIRE(remainder == leash::spec::Money::usd(2.0));
    REQUIRE(remainder.value_or(leash::spec::Money::micros(0)).display() == "$2.0000");
    REQUIRE_FALSE(three.checked_sub(leash::spec::Money::usd(4.0)).has_value());
}

TEST_CASE("usage totals all four token counters", "[spec][usage]") {
    leash::spec::Usage usage;
    usage.input_tokens = 10;
    usage.output_tokens = 20;
    usage.cache_read_input_tokens = 30;
    usage.cache_creation_input_tokens = 40;
    REQUIRE(usage.total_tokens() == 100);
}
