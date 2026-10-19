// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <asio/co_spawn.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_future.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "leash/drivers/driver.hpp"
#include "leash/drivers/mock.hpp"

namespace {

using namespace std::chrono_literals;
using leash::drivers::AskRequest;
using leash::drivers::AskResponse;
using leash::drivers::DriverCtx;
using leash::drivers::DriverError;
using leash::drivers::DriverEvent;
using leash::drivers::DriverRegistry;
using leash::drivers::EventSink;
using leash::drivers::MockDriver;
using leash::drivers::PayloadError;
using leash::drivers::PayloadRef;
using leash::drivers::PayloadSink;
using leash::drivers::RunRequest;
using leash::drivers::RunResponse;
using leash::drivers::StopReason;
using leash::spec::PayloadRefVisitor;

constexpr std::string_view kFixtureDir = LEASH_MOCK_FIXTURE_DIR;

class MemorySink final : public PayloadSink {
  public:
    std::expected<PayloadRef, PayloadError> put(std::span<const std::byte> bytes) override {
        stored = bytes.size();
        return PayloadRef{PayloadRef::Inline{nlohmann::json("stored")}};
    }

    std::size_t stored = 0;
};

class NullEventSink final : public EventSink {
  public:
    void emit(const DriverEvent& /*event*/) override {}
};

struct Harness {
    Harness()
        : ctx{
              .run_id = "run-test",
              .cancel = cancel,
              .payload_sink = sink,
          } {}

    asio::io_context io;
    asio::cancellation_signal cancel;
    MemorySink sink;
    DriverCtx ctx;
};

[[nodiscard]] std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path(kFixtureDir) / std::string(name);
}

[[nodiscard]] MockDriver load(std::string_view name) {
    auto parsed = MockDriver::from_script(fixture(name));
    if (!parsed) {
        throw std::runtime_error("failed to load mock fixture");
    }
    return std::move(*parsed);
}

template <typename Awaitable> auto await_result(asio::io_context& context, Awaitable awaitable) {
    auto future = asio::co_spawn(context, std::move(awaitable), asio::use_future);
    context.restart();
    context.run();
    return future.get();
}

[[nodiscard]] AskRequest ask_for(std::string prompt) {
    AskRequest request;
    request.prompt = std::move(prompt);
    return request;
}

void expect_http_429(const std::expected<AskResponse, DriverError>& result) {
    REQUIRE_FALSE(result.has_value());
    REQUIRE(std::holds_alternative<DriverError::Http>(result.error().value));
    const auto& http = std::get<DriverError::Http>(result.error().value);
    CHECK(http.status == 429);
    CHECK(http.body == "slow down");
}

} // namespace

TEST_CASE("prompt_hash uses BLAKE3 with the b3 prefix", "[drivers][mock]") {
    using leash::drivers::prompt_hash;

    CHECK(prompt_hash("") == "b3:af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262");
    CHECK(prompt_hash("abc") ==
          "b3:6437b3ac38465133ffb63b75273a8db548c558465d79db03fd359c6cd5bd9d85");
}

TEST_CASE("mock id and capabilities describe the script", "[drivers][mock]") {
    MockDriver driver = load("hello.json");

    CHECK(driver.id().starts_with("mock/mock-large/b3:"));
    CHECK(driver.id().ends_with("/1"));
    CHECK(driver.id() != load("seq.json").id());

    const auto caps = driver.caps();
    CHECK(caps.structured_output);
    CHECK_FALSE(caps.tools);
    CHECK_FALSE(caps.effort);
    CHECK_FALSE(caps.sessions);
    CHECK_FALSE(caps.cost_reporting);
    CHECK(caps.cancellation);
}

TEST_CASE("seq matches calls in order and falls through", "[drivers][mock]") {
    Harness harness;
    MockDriver driver = load("seq.json");

    const AskRequest request = ask_for("hello");
    const auto first = await_result(harness.io, driver.ask(request, harness.ctx));
    REQUIRE(first.has_value());
    CHECK(first->text == "first call");

    const auto second = await_result(harness.io, driver.ask(request, harness.ctx));
    REQUIRE(second.has_value());
    CHECK(second->text == "later call");
}

TEST_CASE("prompt_contains, prompt_hash and any match first entry wins", "[drivers][mock]") {
    Harness harness;
    MockDriver driver = load("match.json");

    const auto contains =
        await_result(harness.io, driver.ask(ask_for("please alpha beta"), harness.ctx));
    REQUIRE(contains.has_value());
    CHECK(contains->text == "contains-alpha");

    const auto hashed = await_result(harness.io, driver.ask(ask_for("abc"), harness.ctx));
    REQUIRE(hashed.has_value());
    CHECK(hashed->text == "hash-abc");

    const auto fallback =
        await_result(harness.io, driver.ask(ask_for("something else"), harness.ctx));
    REQUIRE(fallback.has_value());
    CHECK(fallback->text == "fallback");
}

TEST_CASE("fail.times exhausts and falls through to the next entry", "[drivers][mock]") {
    Harness harness;
    MockDriver driver = load("fail_times.json");
    const AskRequest request = ask_for("retry me");

    expect_http_429(await_result(harness.io, driver.ask(request, harness.ctx)));
    expect_http_429(await_result(harness.io, driver.ask(request, harness.ctx)));

    const auto recovered = await_result(harness.io, driver.ask(request, harness.ctx));
    REQUIRE(recovered.has_value());
    CHECK(recovered->text == "recovered");
}

TEST_CASE("unmatched prompts report an excerpt", "[drivers][mock]") {
    Harness harness;
    MockDriver driver = load("unmatched.json");

    const auto first = await_result(harness.io, driver.ask(ask_for("first"), harness.ctx));
    REQUIRE(first.has_value());

    const std::string long_prompt(200, 'x');
    const auto unmatched = await_result(harness.io, driver.ask(ask_for(long_prompt), harness.ctx));
    REQUIRE_FALSE(unmatched.has_value());
    REQUIRE(std::holds_alternative<DriverError::MockUnmatched>(unmatched.error().value));
    const auto& error = std::get<DriverError::MockUnmatched>(unmatched.error().value);
    CHECK(error.prompt_excerpt.size() == 83);
    CHECK(error.prompt_excerpt.starts_with(std::string(80, 'x')));
}

TEST_CASE("latency_ms waits on the io_context and honours cancellation", "[drivers][mock]") {
    Harness harness;
    MockDriver driver = load("latency.json");

    const AskRequest request = ask_for("wait forever");
    auto future = asio::co_spawn(harness.io, driver.ask(request, harness.ctx), asio::use_future);

    asio::steady_timer canceller(harness.io);
    canceller.expires_after(10ms);
    canceller.async_wait(
        [&harness](const asio::error_code&) { harness.cancel.emit(asio::cancellation_type::all); });

    const auto started = std::chrono::steady_clock::now();
    harness.io.run();
    const auto result = future.get();
    const auto elapsed = std::chrono::steady_clock::now() - started;

    REQUIRE_FALSE(result.has_value());
    CHECK(std::holds_alternative<DriverError::Cancelled>(result.error().value));
    CHECK(elapsed < 100ms);
}

TEST_CASE("usage, json and notes propagate from the script", "[drivers][mock]") {
    Harness harness;
    MockDriver driver = load("hello.json");

    AskRequest request = ask_for("hello");
    request.effort = leash::drivers::Effort::high;
    const auto response = await_result(harness.io, driver.ask(request, harness.ctx));
    REQUIRE(response.has_value());

    CHECK(response->text.find("Leash") != std::string::npos);
    REQUIRE(response->json.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    CHECK(response->json.value().at(0).at("name") == "Leash");

    CHECK(response->usage.input_tokens == 120);
    CHECK(response->usage.output_tokens == 40);
    CHECK(response->usage.cache_read_input_tokens == 10);
    CHECK(response->usage.cache_creation_input_tokens == 5);
    CHECK(response->usage.total_tokens() == 175);
    REQUIRE(response->usage.cost.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    CHECK(response->usage.cost.value().micros_value() == 1234);
    CHECK(response->usage.model == "mock-large");
    CHECK(response->model == "mock-large");

    CHECK(std::holds_alternative<StopReason::EndTurn>(response->stop_reason.value));
    REQUIRE_FALSE(response->notes.empty());
    REQUIRE(response->raw.inline_text() != nullptr);
    CHECK_FALSE(response->raw.empty());
}

TEST_CASE("a scripted refusal becomes StopReason::Refusal", "[drivers][mock]") {
    Harness harness;
    MockDriver driver = load("refusal.json");

    const auto response =
        await_result(harness.io, driver.ask(ask_for("do something bad"), harness.ctx));
    REQUIRE(response.has_value());
    REQUIRE(std::holds_alternative<StopReason::Refusal>(response->stop_reason.value));
    const auto& refusal = std::get<StopReason::Refusal>(response->stop_reason.value);
    REQUIRE(refusal.category.has_value());
    CHECK(refusal.category.value() == "cyber"); // NOLINT(bugprone-unchecked-optional-access)
    REQUIRE(refusal.explanation.has_value());
    CHECK(response->text.empty());
}

TEST_CASE("run is unsupported", "[drivers][mock]") {
    Harness harness;
    MockDriver driver = load("hello.json");
    NullEventSink events;

    const RunRequest request;
    const auto result = await_result(harness.io, driver.run(request, harness.ctx, events));
    REQUIRE_FALSE(result.has_value());
    REQUIRE(std::holds_alternative<DriverError::Unsupported>(result.error().value));
    CHECK(std::get<DriverError::Unsupported>(result.error().value).capability == "run");
}

TEST_CASE("registry files a driver under its id name", "[drivers][mock]") {
    MockDriver driver = load("hello.json");
    const std::string driver_id = driver.id();

    DriverRegistry registry;
    registry.add(std::make_shared<MockDriver>(std::move(driver)));

    CHECK(registry.size() == 1);
    CHECK(registry.contains("mock"));
    const auto found = registry.find("mock");
    REQUIRE(found != nullptr);
    CHECK(found->id() == driver_id);
    CHECK(registry.names() == std::vector<std::string>{"mock"});
    CHECK(registry.find("anthropic") == nullptr);
}

TEST_CASE("PayloadRefVisitor renders inline and CAS references", "[drivers][mock]") {
    const PayloadRef inlined{PayloadRef::Inline{nlohmann::json{{"answer", 42}}}};
    const PayloadRef cas{PayloadRef::Cas{"b3:abcd"}};
    const PayloadRefVisitor visitor;

    CHECK(std::visit(visitor, inlined.value).find("42") != std::string::npos);
    CHECK(std::visit(visitor, cas.value) == "b3:abcd");
}
