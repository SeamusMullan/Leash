// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <asio/as_tuple.hpp>
#include <asio/awaitable.hpp>
#include <asio/error_code.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "leash/drivers/anthropic/consts.hpp"
#include "leash/drivers/anthropic/http_parser.hpp"
#include "leash/drivers/anthropic/wire.hpp"
#include "leash/drivers/driver.hpp"

namespace leash::drivers::anthropic {

/// Transport configuration for the spike. The caller supplies the key; nothing
/// here reads the environment.
struct SpikeConfig {
    std::string base_url = std::string(consts::kBaseUrl);
    std::string api_key;
    std::chrono::steady_clock::duration timeout = std::chrono::minutes(10);
};

enum class SseEventKind : std::uint8_t {
    MessageStart,
    ContentBlockStart,
    ContentBlockDelta,
    ContentBlockStop,
    MessageDelta,
    MessageStop,
    Ping,
    Error,
    Unknown,
};

/// One decoded SSE event. `data` is the joined `data:` lines; `json` is the
/// parsed payload when the payload is valid JSON.
struct SseEvent {
    SseEventKind kind = SseEventKind::Unknown;
    std::string name;
    std::string data;
    nlohmann::json json = nlohmann::json::object();
    bool json_valid = false;
};

/// Pure SSE framing state machine: bytes in, complete events out.
///
/// An event ends at a blank line. Chunks may split a line or an event
/// anywhere, `\n`, `\r\n` and `\r` all terminate a line, multiple `data:`
/// lines are joined with `\n`, and `:` comment lines are ignored.
class SseDecoder {
  public:
    /// Feed an arbitrary chunk. Returns every event completed by this chunk.
    [[nodiscard]] std::vector<SseEvent> feed(std::string_view bytes);

    /// Flush a final event that arrived without its terminating blank line.
    [[nodiscard]] std::vector<SseEvent> finish();

    /// True when a single event outgrew `consts::kMaxEventBytes`. The decoder
    /// stops producing events after that; the client reports a protocol error.
    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }

    void reset();

  private:
    void consume_line(std::string_view line, std::vector<SseEvent>& out);
    void dispatch(std::vector<SseEvent>& out);

    std::string buffer_;
    std::string event_;
    std::string data_;
    bool has_data_ = false;
    bool overflowed_ = false;
};

/// The result of reassembling a stream: text blocks, optional parsed JSON, the
/// last usage report and the stop reason.
struct SseResult {
    std::string text;
    std::string thinking;
    std::optional<nlohmann::json> json;
    Usage usage;
    StopReason stop_reason{StopReason::EndTurn{}};
    std::string model;
    std::string message_id;
    std::vector<std::string> notes;
};

/// Turns `SseEvent`s into an `SseResult`. A JSON parse failure, an `error`
/// event or a truncated stream is returned as `DriverError::Protocol`.
class SseAssembler {
  public:
    explicit SseAssembler(bool want_json);

    [[nodiscard]] std::expected<void, DriverError> consume(const SseEvent& event);

    [[nodiscard]] std::expected<SseResult, DriverError> result() const;

    [[nodiscard]] bool saw_stop() const noexcept { return saw_stop_; }

    /// Re-arm for another stream on the same client.
    void reset(bool want_json);

  private:
    std::expected<void, DriverError> on_message_start(const SseEvent& event);
    std::expected<void, DriverError> on_block_start(const SseEvent& event);
    std::expected<void, DriverError> on_block_delta(const SseEvent& event);
    std::expected<void, DriverError> on_message_delta(const SseEvent& event);
    [[nodiscard]] static DriverError error_from(const SseEvent& event);
    void parse_stop_reason(const std::string& reason, const nlohmann::json& details);
    void note_unknown(std::string_view name);

    bool want_json_ = false;
    bool saw_stop_ = false;
    bool saw_stop_reason_ = false;
    SseResult state_;
};

[[nodiscard]] inline DriverError transport_error(std::string message) {
    return DriverError{DriverError::Transport{std::move(message)}};
}

[[nodiscard]] inline DriverError protocol_error(std::string message) {
    return DriverError{DriverError::Protocol{std::move(message)}};
}

/// The serialised HTTP/1.1 request. The response side is Beast's parser; the
/// request is simple enough to spell out, which keeps Beast out of every TU
/// that includes this header.
[[nodiscard]] inline std::string
build_http_request(const Endpoint& endpoint, const SpikeConfig& config, std::string_view body) {
    std::string head;
    head.reserve(512 + body.size());
    head += "POST ";
    head += endpoint.target;
    head += " HTTP/1.1\r\n";
    head += std::string(consts::kHeaderHost) + ": " + endpoint.host_header() + "\r\n";
    head += std::string(consts::kHeaderContentType) + ": application/json\r\n";
    head +=
        std::string(consts::kHeaderAccept) + ": " + std::string(consts::kContentTypeSse) + "\r\n";
    head += std::string(consts::kHeaderApiKey) + ": " + config.api_key + "\r\n";
    head += std::string(consts::kHeaderAnthropicVersion) + ": " + std::string(consts::kApiVersion) +
            "\r\n";
    head += std::string(consts::kHeaderUserAgent) + ": " + std::string(consts::kUserAgent) + "\r\n";
    head += "connection: close\r\n";
    head += "content-length: " + std::to_string(body.size()) + "\r\n\r\n";
    return head;
}

/// HTTP+SSE client for the Messages API. `Stream` is
/// `asio::ssl::stream<asio::ip::tcp::socket>` in production and a plain
/// `asio::ip::tcp::socket` against the test stub.
///
/// The response is parsed incrementally with Beast's `buffer_body` parser;
/// the whole body is never buffered by the HTTP layer. Each `send` needs a
/// fresh connection; no state carries over except what the caller owns.
template <class Stream> class SseClient {
  public:
    SseClient(Stream& stream, SseDecoder& decoder) : stream_(stream), decoder_(decoder) {}

    // The signature is fixed by the WP-05 interface; the stream, config and
    // request outlive the await.
    // NOLINTBEGIN(cppcoreguidelines-avoid-reference-coroutine-parameters)
    asio::awaitable<std::expected<AskResponse, DriverError>> send(const SpikeConfig& config,
                                                                  const AskRequest& request) {
        // NOLINTEND(cppcoreguidelines-avoid-reference-coroutine-parameters)
        auto endpoint = parse_base_url(config.base_url);
        if (!endpoint) {
            co_return std::unexpected(transport_error("invalid base_url: " + endpoint.error()));
        }
        if (config.api_key.empty()) {
            co_return std::unexpected(
                DriverError{DriverError::NoCredentials{std::vector<std::string>{}}});
        }
        if (config.api_key.find_first_of("\r\n") != std::string::npos) {
            co_return std::unexpected(transport_error("api key contains a line break"));
        }

        WireAsk wire = build_ask(request);
        const std::string body = wire.body.dump();
        std::string request_bytes = build_http_request(*endpoint, config, body);
        request_bytes += body;

        auto [write_error, written] = co_await asio::async_write(
            stream_, asio::buffer(request_bytes), asio::as_tuple(asio::use_awaitable));
        if (write_error) {
            co_return std::unexpected(transport_error("write failed: " + write_error.message()));
        }
        (void)written;

        decoder_.reset();
        parser_ = http::ResponseParser{};
        assembler_.reset(request.schema.has_value());
        state_ = StreamState{};

        auto pumped = co_await pump();
        if (!pumped) {
            co_return std::unexpected(std::move(pumped.error()));
        }

        if (state_.status != 200) {
            if (state_.status == 0) {
                co_return std::unexpected(protocol_error("response completed without headers"));
            }
            DriverError::Http http;
            http.status = static_cast<std::uint16_t>(state_.status);
            http.body = std::move(state_.error_body);
            co_return std::unexpected(DriverError{std::move(http)});
        }

        for (auto& event : decoder_.finish()) {
            auto consumed = assembler_.consume(event);
            if (!consumed) {
                co_return std::unexpected(std::move(consumed.error()));
            }
        }

        auto assembled = assembler_.result();
        if (!assembled) {
            co_return std::unexpected(std::move(assembled.error()));
        }

        AskResponse response;
        response.text = std::move(assembled->text);
        response.json = std::move(assembled->json);
        response.usage = std::move(assembled->usage);
        response.stop_reason = std::move(assembled->stop_reason);
        response.raw = Blob{std::move(state_.raw)};
        response.notes = std::move(wire.notes);
        response.notes.insert(response.notes.end(),
                              std::make_move_iterator(assembled->notes.begin()),
                              std::make_move_iterator(assembled->notes.end()));
        response.model = assembled->model.empty() ? wire.model : assembled->model;
        if (response.usage.model.empty()) {
            response.usage.model = response.model;
        }
        co_return response;
    }

  private:
    struct StreamState {
        std::string pending;
        std::string raw;
        std::string error_body;
        int status = 0;
    };

    /// Read one chunk into `state_.pending`. EOF before the parser is done is
    /// a truncation; Asio errors on a closed socket surface as transport
    /// errors, which `spike::ask` maps to Cancelled/Timeout.
    asio::awaitable<std::expected<void, DriverError>> read_some() {
        auto [error, read] = co_await stream_.async_read_some(asio::buffer(chunk_),
                                                              asio::as_tuple(asio::use_awaitable));
        if (error == asio::error::eof) {
            co_return std::unexpected(
                protocol_error("connection closed before the response completed"));
        }
        if (error) {
            co_return std::unexpected(transport_error("read failed: " + error.message()));
        }
        state_.pending.assign(chunk_.data(), read);
        co_return std::expected<void, DriverError>{};
    }

    /// Feed one parser step to the decoder or to the error body.
    [[nodiscard]] std::expected<void, DriverError> on_step(const http::ParseStep& step) {
        if (step.body.empty()) {
            return {};
        }
        if (state_.status != 0 && state_.status != 200) {
            if (state_.error_body.size() < consts::kErrorBodyLimit) {
                const std::size_t room = consts::kErrorBodyLimit - state_.error_body.size();
                state_.error_body.append(step.body, 0, std::min(room, step.body.size()));
            }
            return {};
        }
        if (state_.status != 200) {
            return {};
        }

        state_.raw.append(step.body);
        const auto events = decoder_.feed(step.body);
        if (decoder_.overflowed()) {
            return std::unexpected(protocol_error("SSE event exceeded the maximum size"));
        }
        for (const auto& event : events) {
            auto consumed = assembler_.consume(event);
            if (!consumed) {
                return std::unexpected(std::move(consumed.error()));
            }
        }
        return {};
    }

    asio::awaitable<std::expected<void, DriverError>> pump() {
        for (;;) {
            if (state_.pending.empty()) {
                auto read = co_await read_some();
                if (!read) {
                    co_return std::unexpected(std::move(read.error()));
                }
            }

            const http::ParseStep step =
                parser_.put(std::span(state_.pending.data(), state_.pending.size()));
            state_.pending.erase(0, step.consumed);

            if (!step.error.empty()) {
                co_return std::unexpected(
                    protocol_error("HTTP response parse error: " + step.error));
            }
            if (step.header_done && state_.status == 0) {
                state_.status = step.status;
            }

            auto handled = on_step(step);
            if (!handled) {
                co_return std::unexpected(std::move(handled.error()));
            }
            if (parser_.done()) {
                break;
            }
            if (step.consumed == 0) {
                co_return std::unexpected(protocol_error("HTTP parser made no progress"));
            }
            if (state_.status != 0 && state_.status != 200 &&
                state_.error_body.size() >= consts::kErrorBodyLimit) {
                break;
            }
        }
        co_return std::expected<void, DriverError>{};
    }

    Stream& stream_;      // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    SseDecoder& decoder_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    http::ResponseParser parser_;
    SseAssembler assembler_{false};
    StreamState state_;
    std::array<char, 16384> chunk_{};
};

} // namespace leash::drivers::anthropic
