// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <future>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../support/golden.hpp"
#include "leash/drivers/anthropic/consts.hpp"
#include "leash/drivers/anthropic/spike.hpp"
#include "leash/drivers/anthropic/sse.hpp"
#include "leash/drivers/anthropic/usage.hpp"
#include "leash/drivers/anthropic/wire.hpp"

namespace {

using namespace std::chrono_literals;
using leash::drivers::AskRequest;
using leash::drivers::AskResponse;
using leash::drivers::Blob;
using leash::drivers::DriverError;
using leash::drivers::Effort;
using leash::drivers::PayloadError;
using leash::drivers::PayloadRef;
using leash::drivers::PayloadSink;
using leash::drivers::StopReason;
using leash::drivers::Usage;
using leash::drivers::anthropic::build_ask;
using leash::drivers::anthropic::parse_base_url;
using leash::drivers::anthropic::SseAssembler;
using leash::drivers::anthropic::SseDecoder;
using leash::drivers::anthropic::SseResult;
using leash::drivers::anthropic::consts::kApiVersion;
using leash::drivers::anthropic::consts::kInlinePayloadLimit;
using leash::drivers::anthropic::spike::spill_raw_payload;
using leash::testkit::read_text_file;

using asio::ip::tcp;

[[nodiscard]] std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path(LEASH_ANTHROPIC_FIXTURE_DIR) / std::string(name);
}

class MemorySink final : public PayloadSink {
  public:
    std::expected<PayloadRef, PayloadError> put(std::span<const std::byte> bytes) override {
        stored = bytes.size();
        return PayloadRef{PayloadRef::Cas{"b3:test-payload"}};
    }

    std::size_t stored = 0;
};

[[nodiscard]] std::expected<SseResult, DriverError> decode(std::string_view bytes, bool want_json,
                                                           bool byte_at_a_time = false) {
    SseDecoder decoder;
    SseAssembler assembler(want_json);

    const auto feed = [&assembler](const std::vector<leash::drivers::anthropic::SseEvent>& events)
        -> std::optional<DriverError> {
        for (const auto& event : events) {
            auto consumed = assembler.consume(event);
            if (!consumed) {
                return consumed.error();
            }
        }
        return std::nullopt;
    };

    if (byte_at_a_time) {
        for (const char byte : bytes) {
            const std::string_view one(&byte, 1);
            if (auto error = feed(decoder.feed(one)); error.has_value()) {
                return std::unexpected(*error);
            }
        }
    } else if (auto error = feed(decoder.feed(bytes)); error.has_value()) {
        return std::unexpected(*error);
    }

    if (auto error = feed(decoder.finish()); error.has_value()) {
        return std::unexpected(*error);
    }
    return assembler.result();
}

[[nodiscard]] std::string json_body(std::string_view body) {
    // Chunked so the client cannot depend on one read per response.
    constexpr std::size_t kChunk = 61;
    std::string out = "HTTP/1.1 200 OK\r\ncontent-type: text/event-stream\r\n"
                      "connection: close\r\ntransfer-encoding: chunked\r\n\r\n";
    for (std::size_t offset = 0; offset < body.size(); offset += kChunk) {
        const std::string_view piece = body.substr(offset, kChunk);
        std::ostringstream size;
        size << std::hex << piece.size() << "\r\n";
        out += size.str();
        out.append(piece);
        out += "\r\n";
    }
    out += "0\r\n\r\n";
    return out;
}

[[nodiscard]] std::string error_body(int status, std::string_view reason, std::string_view body) {
    return "HTTP/1.1 " + std::to_string(status) + " " + std::string(reason) +
           "\r\ncontent-type: application/json\r\ncontent-length: " + std::to_string(body.size()) +
           "\r\nconnection: close\r\n\r\n" + std::string(body);
}

[[nodiscard]] std::string truncated_body() {
    std::string out = "HTTP/1.1 200 OK\r\ncontent-type: text/event-stream\r\n"
                      "connection: close\r\ncontent-length: 4096\r\n\r\n";
    out += "event: message_start\n"
           "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_trunc\","
           "\"type\":\"message\",\"role\":\"assistant\",\"model\":\"claude-opus-5\","
           "\"content\":[],\"usage\":{\"input_tokens\":1,\"output_tokens\":1}}}\n\n";
    out += "event: content_block_start\n"
           "data: {\"type\":\"content_block_start\",\"index\":0,"
           "\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n";
    out += "event: content_block_delta\n"
           "data: {\"type\":\"content_block_delta\",\"index\":0,"
           "\"delta\":{\"type\":\"text_delta\",\"text\":\"partial\"}}\n\n";
    return out;
}

/// A local HTTP stub: accepts one connection per queued response, reads the
/// whole request (headers and content-length body), writes the response, then
/// drains until the client closes.
class StubServer {
  public:
    explicit StubServer(asio::io_context& ctx) : acceptor_(ctx) {
        asio::error_code error;
        check(acceptor_.open(tcp::v4(), error));
        check(error);
        check(acceptor_.set_option(tcp::acceptor::reuse_address(true), error));
        check(acceptor_.bind(tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0), error));
        check(error);
        check(acceptor_.listen(asio::socket_base::max_listen_connections, error));
        check(error);
    }

    void queue(std::string response) { responses_.push_back(std::move(response)); }

    asio::awaitable<void> run() {
        for (const std::string& response : responses_) {
            auto socket = co_await acceptor_.async_accept(asio::use_awaitable);
            std::string request;
            std::array<char, 4096> buffer{};

            while (!request_complete(request)) {
                auto [error, read] = co_await socket.async_read_some(
                    asio::buffer(buffer), asio::as_tuple(asio::use_awaitable));
                if (error) {
                    break;
                }
                request.append(buffer.data(), read);
            }
            last_request_ = request;

            co_await asio::async_write(socket, asio::buffer(response), asio::use_awaitable);

            asio::error_code ignored;
            [[maybe_unused]] const asio::error_code shutdown_result =
                socket.shutdown(tcp::socket::shutdown_send, ignored);
            while (true) {
                auto [error, read] = co_await socket.async_read_some(
                    asio::buffer(buffer), asio::as_tuple(asio::use_awaitable));
                if (error) {
                    break;
                }
                (void)read;
            }
        }
    }

    [[nodiscard]] std::uint16_t port() const { return acceptor_.local_endpoint().port(); }

    [[nodiscard]] const std::string& last_request() const { return last_request_; }

  private:
    static void check(const asio::error_code& error) { REQUIRE_FALSE(error); }

    static bool request_complete(const std::string& request) {
        const std::size_t headers = request.find("\r\n\r\n");
        if (headers == std::string::npos) {
            return false;
        }
        std::size_t length = 0;
        const std::size_t field = request.find("content-length:");
        if (field != std::string::npos && field < headers) {
            length = std::stoul(request.substr(field + 15));
        }
        return request.size() >= headers + 4 + length;
    }

    tcp::acceptor acceptor_;
    std::vector<std::string> responses_;
    std::string last_request_;
};

// NOLINTBEGIN(cppcoreguidelines-avoid-reference-coroutine-parameters)
[[nodiscard]] asio::awaitable<std::expected<AskResponse, DriverError>>
ask_over_stub(std::uint16_t port, const AskRequest& request, SseDecoder& decoder) {
    // NOLINTEND(cppcoreguidelines-avoid-reference-coroutine-parameters)
    tcp::socket socket(co_await asio::this_coro::executor);
    co_await socket.async_connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port),
                                  asio::use_awaitable);

    leash::drivers::anthropic::SpikeConfig config;
    config.base_url = "http://127.0.0.1:" + std::to_string(port);
    config.api_key = "test-key";

    leash::drivers::anthropic::SseClient<tcp::socket> client(socket, decoder);
    co_return co_await client.send(config, request);
}

struct TwoAsks {
    std::expected<AskResponse, DriverError> first;
    std::expected<AskResponse, DriverError> second;
};

/// Two calls over fresh connections, sequentially, so the stub's queued
/// responses are consumed in a deterministic order.
// NOLINTBEGIN(cppcoreguidelines-avoid-reference-coroutine-parameters)
[[nodiscard]] asio::awaitable<TwoAsks> ask_twice_over_stub(std::uint16_t port,
                                                           const AskRequest& request) {
    // NOLINTEND(cppcoreguidelines-avoid-reference-coroutine-parameters)
    SseDecoder first_decoder;
    auto first = co_await ask_over_stub(port, request, first_decoder);
    SseDecoder second_decoder;
    auto second = co_await ask_over_stub(port, request, second_decoder);
    co_return TwoAsks{.first = std::move(first), .second = std::move(second)};
}

struct RecordedBodies {
    std::string first;
    std::string second;
};

[[nodiscard]] std::string trim_newlines(std::string text) {
    while (!text.empty() && (text.front() == '\n' || text.front() == '\r')) {
        text.erase(0, 1);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

/// Split the two-body rate-limit fixture on its markers.
[[nodiscard]] RecordedBodies split_recorded_bodies(std::string_view recorded) {
    constexpr std::string_view kFirstMarker = "=== body 1: HTTP 429 ===";
    constexpr std::string_view kSecondMarker = "=== body 2: HTTP 200 ===";
    const std::size_t first_at = recorded.find(kFirstMarker);
    const std::size_t second_at = recorded.find(kSecondMarker);
    REQUIRE(first_at != std::string_view::npos);
    REQUIRE(second_at != std::string_view::npos);

    const std::size_t first_start = first_at + kFirstMarker.size();
    return RecordedBodies{
        .first = trim_newlines(std::string(recorded.substr(first_start, second_at - first_start))),
        .second = trim_newlines(std::string(recorded.substr(second_at + kSecondMarker.size()))),
    };
}

[[nodiscard]] AskRequest text_request() {
    AskRequest request;
    request.system = {
        leash::drivers::PromptBlock{"You are Leash."},
    };
    request.prompt = "What is a harness?";
    return request;
}

} // namespace

[[nodiscard]] nlohmann::json require_json(const std::optional<nlohmann::json>& value) {
    REQUIRE(value.has_value());
    return value.value_or(nlohmann::json::object());
}

TEST_CASE("anthropic ask request golden", "[drivers][anthropic]") {
    AskRequest request;
    request.system = {
        leash::drivers::PromptBlock{"You are Leash."},
        leash::drivers::PromptBlock{"Return JSON only."},
    };
    request.prompt = "Summarise the harness in one paragraph.";
    request.schema = nlohmann::json::parse(R"({
        "type": "object",
        "properties": {"answer": {"type": "integer"}},
        "required": ["answer"],
        "additionalProperties": false
    })");

    const auto wire = build_ask(request);
    CHECK(wire.model == "claude-opus-5");
    CHECK(wire.max_tokens == 16000);

    const std::string rendered = wire.body.dump(2) + "\n";
    leash::testkit::expect_golden(fixture("ask_request.json"), rendered);

    const nlohmann::json& body = wire.body;
    CHECK(body.at("stream") == true);
    CHECK(body.at("thinking").at("type") == "adaptive");
    CHECK(body.at("output_config").at("effort") == "high");
    CHECK(body.at("output_config").at("format").at("type") == "json_schema");
    CHECK(body.at("output_config").at("format").at("schema") == *request.schema);
    REQUIRE(body.at("system").size() == 2);
    CHECK_FALSE(body.at("system").at(0).contains("cache_control"));
    CHECK(body.at("system").at(1).at("cache_control").at("type") == "ephemeral");
    CHECK(body.at("messages").at(0).at("role") == "user");
    CHECK(body.at("messages").at(0).at("content") == request.prompt);
}

TEST_CASE("anthropic ask request never sends rejected options", "[drivers][anthropic]") {
    AskRequest request;
    request.system = {leash::drivers::PromptBlock{"system"}};
    request.prompt = "prompt";
    request.effort = Effort::low;
    request.max_tokens = 512;

    const auto wire = build_ask(request);
    const std::string rendered = wire.body.dump();

    CHECK(rendered.find("temperature") == std::string::npos);
    CHECK(rendered.find("top_p") == std::string::npos);
    CHECK(rendered.find("budget_tokens") == std::string::npos);
    CHECK(rendered.find("assistant") == std::string::npos);
    CHECK(wire.body.at("output_config").at("effort") == "low");
    CHECK_FALSE(wire.body.at("output_config").contains("format"));
    CHECK(wire.body.at("max_tokens") == 512);

    // Exactly one breakpoint, and it is on the last block.
    std::size_t breakpoints = 0;
    for (std::size_t position = rendered.find("cache_control"); position != std::string::npos;
         position = rendered.find("cache_control", position + 1)) {
        ++breakpoints;
    }
    CHECK(breakpoints == 1);
}

TEST_CASE("anthropic base url parsing", "[drivers][anthropic]") {
    const auto defaulted = parse_base_url("https://api.anthropic.com");
    REQUIRE(defaulted.has_value());
    CHECK(defaulted->scheme == "https");
    CHECK(defaulted->host == "api.anthropic.com");
    CHECK(defaulted->port == "443");
    CHECK(defaulted->target == "/v1/messages");
    CHECK(defaulted->host_header() == "api.anthropic.com");
    CHECK(defaulted->tls());

    const auto local = parse_base_url("http://127.0.0.1:8080/");
    REQUIRE(local.has_value());
    CHECK(local->port == "8080");
    CHECK(local->target == "/v1/messages");
    CHECK(local->host_header() == "127.0.0.1:8080");
    CHECK_FALSE(local->tls());

    const auto prefixed = parse_base_url("https://example.test/proxy/v1/messages");
    REQUIRE(prefixed.has_value());
    CHECK(prefixed->target == "/proxy/v1/messages");

    const auto ipv6 = parse_base_url("https://[::1]:8443");
    REQUIRE(ipv6.has_value());
    CHECK(ipv6->host == "::1");
    CHECK(ipv6->host_header() == "[::1]:8443");

    CHECK_FALSE(parse_base_url("api.anthropic.com").has_value());
    CHECK_FALSE(parse_base_url("ftp://api.anthropic.com").has_value());
    CHECK_FALSE(parse_base_url("https://host:notaport").has_value());
}

TEST_CASE("sse decoder handles framing rules", "[drivers][anthropic]") {
    const std::string stream = ": comment line\r\n"
                               "\r\n"
                               "event: content_block_delta\r\n"
                               "data: {\"a\":\r\n"
                               "data: 1}\r\n"
                               "\r\n"
                               "event: ping\n"
                               "data: {\"type\":\"ping\"}\n"
                               "\n";

    SseDecoder decoder;
    const auto events = decoder.feed(stream);
    REQUIRE(events.size() == 2);
    CHECK(events.at(0).name == "content_block_delta");
    CHECK(events.at(0).data == "{\"a\":\n1}");
    REQUIRE(events.at(0).json_valid);
    CHECK(events.at(0).json.at("a") == 1);
    CHECK(events.at(1).kind == leash::drivers::anthropic::SseEventKind::Ping);
    CHECK(decoder.finish().empty());
}

TEST_CASE("sse decoder survives every split point", "[drivers][anthropic]") {
    const std::string bytes = read_text_file(fixture("ask_text.sse"));

    SseDecoder whole;
    const auto whole_events = whole.feed(bytes);
    REQUIRE_FALSE(whole_events.empty());

    SseDecoder split;
    std::vector<leash::drivers::anthropic::SseEvent> split_events;
    for (const char byte : bytes) {
        const std::string_view one(&byte, 1);
        auto events = split.feed(one);
        split_events.insert(split_events.end(), std::make_move_iterator(events.begin()),
                            std::make_move_iterator(events.end()));
    }
    REQUIRE(split_events.size() == whole_events.size());
    for (std::size_t index = 0; index < whole_events.size(); ++index) {
        CHECK(split_events.at(index).kind == whole_events.at(index).kind);
        CHECK(split_events.at(index).data == whole_events.at(index).data);
    }
}

TEST_CASE("ask_text fixture reassembles text and usage", "[drivers][anthropic]") {
    const auto result = decode(read_text_file(fixture("ask_text.sse")), false);
    REQUIRE(result.has_value());
    CHECK(result->text == "A harness is the part of the agent that is not the model. It decides "
                          "what the model sees, what it may do, and how its output is checked.");
    CHECK_FALSE(result->json.has_value());
    CHECK(result->usage.input_tokens == 120);
    CHECK(result->usage.output_tokens == 42);
    CHECK(result->usage.cache_read_input_tokens == 1024);
    CHECK(result->usage.cache_creation_input_tokens == 256);
    CHECK(result->usage.model == "claude-opus-5");
    CHECK(result->model == "claude-opus-5");
    CHECK(std::holds_alternative<StopReason::EndTurn>(result->stop_reason.value));
}

TEST_CASE("ask_text fixture decodes identically one byte at a time", "[drivers][anthropic]") {
    const auto result = decode(read_text_file(fixture("ask_text.sse")), false, true);
    REQUIRE(result.has_value());
    CHECK(result->usage.output_tokens == 42);
    CHECK(result->text.starts_with("A harness is the part"));
    CHECK(std::holds_alternative<StopReason::EndTurn>(result->stop_reason.value));
}

TEST_CASE("ask_json fixture parses the structured answer", "[drivers][anthropic]") {
    const auto result = decode(read_text_file(fixture("ask_json.sse")), true);
    REQUIRE(result.has_value());
    const nlohmann::json parsed = require_json(result->json);
    CHECK(parsed.at("answer") == 42);
    CHECK(parsed.at("why") == "because the harness supplies the loop");
    CHECK(result->usage.input_tokens == 210);
    CHECK(result->usage.output_tokens == 37);
    CHECK(result->text.front() == '{');
}

TEST_CASE("schema output that is not JSON is a protocol error", "[drivers][anthropic]") {
    const auto result = decode(read_text_file(fixture("ask_text.sse")), true);
    REQUIRE_FALSE(result.has_value());
    CHECK(std::holds_alternative<DriverError::Protocol>(result.error().value));
}

TEST_CASE("refusal fixture maps stop_details", "[drivers][anthropic]") {
    const auto result = decode(read_text_file(fixture("refusal.sse")), false);
    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<StopReason::Refusal>(result->stop_reason.value));
    const auto& refusal = std::get<StopReason::Refusal>(result->stop_reason.value);
    const std::string category = refusal.category.value_or("");
    const std::string explanation = refusal.explanation.value_or("");
    CHECK(category == "cyber");
    CHECK(explanation.find("offensive tooling") != std::string::npos);
    CHECK(result->text == "I can't help with that.");
}

TEST_CASE("refusal is not treated as a structured-output failure", "[drivers][anthropic]") {
    const auto result = decode(read_text_file(fixture("refusal.sse")), true);
    REQUIRE(result.has_value());
    CHECK(std::holds_alternative<StopReason::Refusal>(result->stop_reason.value));
    CHECK_FALSE(result->json.has_value());
}

TEST_CASE("error event becomes DriverError::Protocol with the event message",
          "[drivers][anthropic]") {
    const auto result = decode(read_text_file(fixture("error_event.sse")), false);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(std::holds_alternative<DriverError::Protocol>(result.error().value));
    CHECK(std::get<DriverError::Protocol>(result.error().value).message == "Overloaded");
}

TEST_CASE("truncated stream is a protocol error", "[drivers][anthropic]") {
    std::string stream = read_text_file(fixture("ask_text.sse"));
    const std::size_t stop = stream.find("event: message_stop");
    REQUIRE(stop != std::string::npos);
    stream.erase(stop);

    const auto result = decode(stream, false);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(std::holds_alternative<DriverError::Protocol>(result.error().value));
    CHECK(std::get<DriverError::Protocol>(result.error().value).message.find("message_stop") !=
          std::string::npos);
}

TEST_CASE("usage takes the last cumulative output_tokens and never sums", "[drivers][anthropic]") {
    Usage usage;
    leash::drivers::anthropic::apply_message_start(usage, nlohmann::json::parse(R"({
            "model": "claude-opus-5",
            "usage": {"input_tokens": 100, "output_tokens": 1,
                      "cache_read_input_tokens": 30, "cache_creation_input_tokens": 7}
        })"));
    CHECK(usage.input_tokens == 100);
    CHECK(usage.output_tokens == 1);
    CHECK(usage.model == "claude-opus-5");

    leash::drivers::anthropic::apply_message_delta(
        usage, nlohmann::json::parse(R"({"output_tokens": 7})"));
    leash::drivers::anthropic::apply_message_delta(
        usage, nlohmann::json::parse(R"({"output_tokens": 9})"));

    CHECK(usage.output_tokens == 9);
    CHECK(usage.input_tokens == 100);
    CHECK(usage.cache_read_input_tokens == 30);
    CHECK(usage.cache_creation_input_tokens == 7);
}

TEST_CASE("raw payloads above 8 KiB go to the payload sink", "[drivers][anthropic]") {
    MemorySink sink;

    AskResponse small;
    small.raw = Blob{std::string(kInlinePayloadLimit, 'x')};
    auto kept = spill_raw_payload(small, sink);
    REQUIRE(kept.has_value());
    CHECK(sink.stored == 0);
    REQUIRE(small.raw.inline_text() != nullptr);

    AskResponse large;
    large.raw = Blob{std::string(kInlinePayloadLimit + 1, 'x')};
    auto spilled = spill_raw_payload(large, sink);
    REQUIRE(spilled.has_value());
    CHECK(sink.stored == kInlinePayloadLimit + 1);
    CHECK(large.raw.inline_text() == nullptr);
    REQUIRE(large.raw.ref() != nullptr);
    CHECK(std::get<PayloadRef::Cas>(large.raw.ref()->value).hash == "b3:test-payload");
}

TEST_CASE("stub server transport returns the streamed answer", "[drivers][anthropic]") {
    asio::io_context ctx;
    StubServer server(ctx);
    server.queue(json_body(read_text_file(fixture("ask_text.sse"))));

    SseDecoder decoder;
    AskRequest request = text_request();
    auto client =
        asio::co_spawn(ctx, ask_over_stub(server.port(), request, decoder), asio::use_future);
    auto serving = asio::co_spawn(ctx, server.run(), asio::use_future);
    ctx.run();

    const auto response = client.get();
    serving.get();

    REQUIRE(response.has_value());
    CHECK(response->text.starts_with("A harness is the part"));
    CHECK(response->usage.output_tokens == 42);
    CHECK(std::holds_alternative<StopReason::EndTurn>(response->stop_reason.value));

    const std::string& sent = server.last_request();
    CHECK(sent.starts_with("POST /v1/messages HTTP/1.1\r\n"));
    CHECK(sent.find("x-api-key: test-key") != std::string::npos);
    CHECK(sent.find("anthropic-version: " + std::string(kApiVersion)) != std::string::npos);
    CHECK(sent.find("accept: text/event-stream") != std::string::npos);
    CHECK(sent.find("\"stream\":true") != std::string::npos);
}

TEST_CASE("stub server 429 then 200 proves the client is re-drivable", "[drivers][anthropic]") {
    const RecordedBodies bodies =
        split_recorded_bodies(read_text_file(fixture("rate_limited_then_ok.sse")));

    asio::io_context ctx;
    StubServer server(ctx);
    server.queue(error_body(429, "Too Many Requests", bodies.first));
    server.queue(json_body(bodies.second));

    auto serving = asio::co_spawn(ctx, server.run(), asio::use_future);

    AskRequest request = text_request();
    auto client =
        asio::co_spawn(ctx, ask_twice_over_stub(server.port(), request), asio::use_future);
    ctx.run();
    const auto both = client.get();
    serving.get();

    REQUIRE_FALSE(both.first.has_value());
    REQUIRE(std::holds_alternative<DriverError::Http>(both.first.error().value));
    CHECK(std::get<DriverError::Http>(both.first.error().value).status == 429);
    CHECK(std::get<DriverError::Http>(both.first.error().value).body.find("rate_limit_error") !=
          std::string::npos);

    REQUIRE(both.second.has_value());
    CHECK(both.second->text == "Second attempt succeeds.");
    CHECK(both.second->usage.output_tokens == 5);
}

TEST_CASE("stub server that truncates the body is a protocol error", "[drivers][anthropic]") {
    asio::io_context ctx;
    StubServer server(ctx);
    server.queue(truncated_body());

    SseDecoder decoder;
    AskRequest request = text_request();
    auto client =
        asio::co_spawn(ctx, ask_over_stub(server.port(), request, decoder), asio::use_future);
    auto serving = asio::co_spawn(ctx, server.run(), asio::use_future);
    ctx.run();

    const auto response = client.get();
    serving.get();

    REQUIRE_FALSE(response.has_value());
    INFO(std::get<DriverError::Protocol>(response.error().value).message);
    REQUIRE(std::holds_alternative<DriverError::Protocol>(response.error().value));
    CHECK(std::get<DriverError::Protocol>(response.error().value).message.find("before") !=
          std::string::npos);
}
