// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "leash/drivers/anthropic/spike.hpp"

namespace {

using namespace std::chrono_literals;
using leash::drivers::AskRequest;
using leash::drivers::DriverCtx;
using leash::drivers::DriverError;
using leash::drivers::PayloadError;
using leash::drivers::PayloadRef;
using leash::drivers::PayloadSink;

class NullSink final : public PayloadSink {
  public:
    std::expected<PayloadRef, PayloadError> put(std::span<const std::byte> /*bytes*/) override {
        return PayloadRef{PayloadRef::Cas{"b3:live-test"}};
    }
};

[[nodiscard]] std::string describe(const DriverError& error) {
    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, DriverError::NoCredentials>) {
                return "NoCredentials";
            } else if constexpr (std::is_same_v<T, DriverError::Http>) {
                return "Http " + std::to_string(value.status) + ": " + value.body;
            } else if constexpr (std::is_same_v<T, DriverError::Transport>) {
                return "Transport: " + value.message;
            } else if constexpr (std::is_same_v<T, DriverError::Protocol>) {
                return "Protocol: " + value.message;
            } else if constexpr (std::is_same_v<T, DriverError::Unsupported>) {
                return "Unsupported: " + value.capability;
            } else if constexpr (std::is_same_v<T, DriverError::MockUnmatched>) {
                return "MockUnmatched";
            } else if constexpr (std::is_same_v<T, DriverError::Cancelled>) {
                return "Cancelled";
            } else {
                return "Timeout";
            }
        },
        error.value);
}

[[nodiscard]] nlohmann::json require_json(const std::optional<nlohmann::json>& value) {
    REQUIRE(value.has_value());
    return value.value_or(nlohmann::json::object());
}

} // namespace

// Catch2 expands TEST_CASE into one long function; the linter cannot see the
// individual test scope.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("anthropic live schema ask", "[network][anthropic]") {
    const char* network = std::getenv("LEASH_TEST_NETWORK"); // NOLINT(concurrency-mt-unsafe)
    const char* key = std::getenv("ANTHROPIC_API_KEY");      // NOLINT(concurrency-mt-unsafe)
    if (network == nullptr || std::string_view{network} != "1" || key == nullptr || *key == '\0') {
        SKIP("set LEASH_TEST_NETWORK=1 and ANTHROPIC_API_KEY to run the live Anthropic smoke test");
    }

    asio::io_context io_context;
    asio::cancellation_signal cancel;
    NullSink sink;
    DriverCtx ctx{
        .run_id = "live-anthropic-sse",
        .cancel = cancel,
        .payload_sink = sink,
    };

    leash::drivers::anthropic::SpikeConfig config;
    config.api_key = key;
    config.timeout = 2min;

    AskRequest request;
    request.model = "claude-opus-5";
    request.prompt = "Reply with a single JSON object and nothing else.";
    request.schema = nlohmann::json::parse(R"({
        "type": "object",
        "properties": {"answer": {"type": "integer"}},
        "required": ["answer"],
        "additionalProperties": false
    })");

    auto future = asio::co_spawn(
        io_context, leash::drivers::anthropic::spike::ask(config, request, ctx), asio::use_future);
    io_context.run();
    const auto result = future.get();

    if (!result) {
        FAIL("ask failed: " << describe(result.error()));
    }
    REQUIRE(result.has_value());
    const nlohmann::json parsed = require_json(result->json);
    CHECK(parsed.at("answer").is_number_integer());
    CHECK(result->usage.input_tokens > 0);
    CHECK(result->usage.output_tokens > 0);
    CHECK(result->model == "claude-opus-5");
    CHECK_FALSE(result->text.empty());
}
