// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <asio/awaitable.hpp>

#include <expected>

#include "leash/drivers/anthropic/sse.hpp" // SpikeConfig, SseClient
#include "leash/drivers/driver.hpp"

namespace leash::drivers::anthropic::spike {

/// Transport spike: one streamed `ask` over TLS to the Messages API.
///
/// Not registered in `DriverRegistry` and not a `Driver`; WP-13 wraps this into
/// the `anthropic` driver with credentials, configuration and retries.
///
/// `config.base_url` must be `https://` (the stub-server transport test drives
/// `SseClient<tcp::socket>` directly). SNI is set and certificate verification
/// stays on. Cancellation via `ctx.cancel` and `config.timeout` close the socket
/// and complete the awaitable with `DriverError::Cancelled` / `Timeout`.
///
/// A raw body above `consts::kInlinePayloadLimit` is stored through
/// `ctx.payload_sink` and referenced from `AskResponse::raw`; otherwise it
/// travels inline.
asio::awaitable<std::expected<AskResponse, DriverError>>
ask(const SpikeConfig& config, const AskRequest& request, DriverCtx& ctx);

/// Apply the large-payload rule to an already assembled response: a raw body
/// above `consts::kInlinePayloadLimit` is stored through `sink` and
/// `response.raw` becomes the CAS reference; smaller bodies stay inline.
/// Exposed so the rule is unit-testable without a TLS server.
std::expected<void, DriverError> spill_raw_payload(AskResponse& response, PayloadSink& sink);

} // namespace leash::drivers::anthropic::spike
