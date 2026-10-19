// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/drivers/anthropic/spike.hpp"

#include <asio/as_tuple.hpp>
#include <asio/connect.hpp>
#include <asio/error_code.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>
#include <asio/ssl.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <openssl/ssl.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "leash/drivers/anthropic/consts.hpp"

namespace leash::drivers::anthropic::spike {

namespace {

using asio::ip::tcp;
using TlsStream = asio::ssl::stream<tcp::socket>;

struct AbortState {
    std::atomic<bool> cancelled{false};
    std::atomic<bool> timed_out{false};
    std::atomic<bool> done{false};
};

[[nodiscard]] std::optional<DriverError> abort_error(const AbortState& state) {
    if (state.cancelled.load()) {
        return DriverError{DriverError::Cancelled{}};
    }
    if (state.timed_out.load()) {
        return DriverError{DriverError::Timeout{}};
    }
    return std::nullopt;
}

/// Runs the request on an established stream. Kept separate so `ask` can map
/// an operation-aborted result back to Cancelled/Timeout after the timer and
/// cancellation slot have been unwound.
template <class Stream>
// NOLINTBEGIN(cppcoreguidelines-avoid-reference-coroutine-parameters)
asio::awaitable<std::expected<AskResponse, DriverError>>
run_request(Stream& stream, const SpikeConfig& config, const AskRequest& request) {
    // NOLINTEND(cppcoreguidelines-avoid-reference-coroutine-parameters)
    SseDecoder decoder;
    SseClient<Stream> client(stream, decoder);
    co_return co_await client.send(config, request);
}

/// Resolve, connect, set SNI and complete the TLS handshake. Failures here are
/// transport errors; `ask` checks the abort flags before reporting them.
// NOLINTBEGIN(cppcoreguidelines-avoid-reference-coroutine-parameters)
asio::awaitable<std::expected<void, DriverError>> establish(TlsStream& stream,
                                                            const Endpoint& endpoint) {
    // NOLINTEND(cppcoreguidelines-avoid-reference-coroutine-parameters)
    tcp::resolver resolver(stream.get_executor());
    auto resolved = co_await resolver.async_resolve(endpoint.host, endpoint.port,
                                                    asio::as_tuple(asio::use_awaitable));
    if (std::get<0>(resolved)) {
        co_return std::unexpected(
            transport_error("resolve failed: " + std::get<0>(resolved).message()));
    }

    auto connected = co_await asio::async_connect(stream.lowest_layer(), std::get<1>(resolved),
                                                  asio::as_tuple(asio::use_awaitable));
    if (std::get<0>(connected)) {
        co_return std::unexpected(
            transport_error("connect failed: " + std::get<0>(connected).message()));
    }

    if (SSL_set_tlsext_host_name(stream.native_handle(), endpoint.host.c_str()) != 1) {
        co_return std::unexpected(transport_error("cannot set TLS SNI host name"));
    }

    auto handshake = co_await stream.async_handshake(asio::ssl::stream_base::client,
                                                     asio::as_tuple(asio::use_awaitable));
    if (std::get<0>(handshake)) {
        co_return std::unexpected(
            transport_error("TLS handshake failed: " + std::get<0>(handshake).message()));
    }
    co_return std::expected<void, DriverError>{};
}

} // namespace

// The driver contract fixes this signature; the caller owns the referenced
// objects for the whole await.
// NOLINTBEGIN(cppcoreguidelines-avoid-reference-coroutine-parameters)
asio::awaitable<std::expected<AskResponse, DriverError>>
ask(const SpikeConfig& config, const AskRequest& request, DriverCtx& ctx) {
    // NOLINTEND(cppcoreguidelines-avoid-reference-coroutine-parameters)
    if (config.api_key.empty()) {
        co_return std::unexpected(
            DriverError{DriverError::NoCredentials{std::vector<std::string>{}}});
    }

    auto endpoint = parse_base_url(config.base_url);
    if (!endpoint) {
        co_return std::unexpected(transport_error("invalid base_url: " + endpoint.error()));
    }
    if (!endpoint->tls()) {
        co_return std::unexpected(
            transport_error("the anthropic spike requires an https base_url"));
    }

    const auto executor = co_await asio::this_coro::executor;

    asio::ssl::context context(asio::ssl::context::tls_client);
    context.set_verify_mode(asio::ssl::verify_peer);
    asio::error_code ca_error;
    const asio::error_code ca_result = context.set_default_verify_paths(ca_error);
    if (ca_error || ca_result) {
        co_return std::unexpected(
            transport_error("cannot load the system CA bundle: " + ca_error.message()));
    }

    TlsStream stream(executor, context);
    stream.set_verify_callback(asio::ssl::host_name_verification(endpoint->host));

    auto state = std::make_shared<AbortState>();

    asio::steady_timer timer(executor);
    timer.expires_after(config.timeout);
    timer.async_wait([state, &stream](const asio::error_code& error) {
        if (error) {
            return;
        }
        state->timed_out.store(true);
        asio::error_code ignored;
        [[maybe_unused]] const asio::error_code closed = stream.lowest_layer().close(ignored);
    });

    asio::cancellation_slot slot = ctx.cancel.slot();
    slot.assign([state, &stream, executor](asio::cancellation_type /*type*/) {
        state->cancelled.store(true);
        // Asio may complete the pending op from a foreign thread; post the
        // close so it never races the socket's executor.
        asio::post(executor, [state, &stream] {
            if (state->done.load()) {
                return;
            }
            asio::error_code ignored;
            [[maybe_unused]] const asio::error_code closed = stream.lowest_layer().close(ignored);
        });
    });

    auto finish = [&] {
        state->done.store(true);
        timer.cancel();
        slot.clear();
    };

    auto established = co_await establish(stream, *endpoint);
    if (!established) {
        if (auto aborted = abort_error(*state); aborted.has_value()) {
            finish();
            co_return std::unexpected(std::move(*aborted));
        }
        finish();
        co_return std::unexpected(std::move(established.error()));
    }

    auto result = co_await run_request(stream, config, request);

    finish();
    // Closing instead of async_shutdown: a peer that truncated the stream may
    // never send close_notify and the graceful wait would hang.
    asio::error_code ignored;
    [[maybe_unused]] const asio::error_code closed = stream.lowest_layer().close(ignored);

    if (auto aborted = abort_error(*state); aborted.has_value()) {
        co_return std::unexpected(std::move(*aborted));
    }
    if (!result) {
        co_return std::unexpected(std::move(result.error()));
    }

    auto spilled = spill_raw_payload(*result, ctx.payload_sink);
    if (!spilled) {
        co_return std::unexpected(std::move(spilled.error()));
    }

    co_return std::move(*result);
}

std::expected<void, DriverError> spill_raw_payload(AskResponse& response, PayloadSink& sink) {
    const std::string* raw = response.raw.inline_text();
    if (raw == nullptr || raw->size() <= consts::kInlinePayloadLimit) {
        return {};
    }

    const std::string bytes = *raw;
    auto stored = sink.put(std::as_bytes(std::span<const char>(bytes.data(), bytes.size())));
    if (!stored) {
        return std::unexpected(
            protocol_error("payload store rejected the response body: " + stored.error().message));
    }
    response.raw = Blob{std::move(*stored)};
    response.notes.push_back("raw response body stored (" + std::to_string(bytes.size()) +
                             " bytes)");
    return {};
}

} // namespace leash::drivers::anthropic::spike
