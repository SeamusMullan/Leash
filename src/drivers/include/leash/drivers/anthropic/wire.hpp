// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "leash/drivers/driver.hpp"

namespace leash::drivers::anthropic {

/// A base URL split into the pieces the HTTP client needs.
struct Endpoint {
    std::string scheme; // "https" or "http"
    std::string host;
    std::string port;   // decimal, defaulted from the scheme
    std::string target; // path + query of the POST target

    [[nodiscard]] bool tls() const noexcept;
    [[nodiscard]] std::string host_header() const;
};

/// Split `https://host[:port][/prefix]` into an endpoint and append the
/// Messages path to the prefix. `std::unexpected` carries a human-readable
/// reason (used only for configuration errors; never contains the key).
[[nodiscard]] std::expected<Endpoint, std::string> parse_base_url(std::string_view base_url);

/// The resolved request body plus the values the caller needs to build the
/// driver id and to interpret the response.
struct WireAsk {
    nlohmann::json body;
    std::string model;
    std::uint32_t max_tokens = 0;
    /// Caller options the `ask` transport cannot honour. Surfaced in
    /// `AskResponse::notes` rather than silently dropped.
    std::vector<std::string> notes;
};

/// Build the `POST /v1/messages` body for the `ask` subset of docs/09 §2.
///
/// Always: `model`, `max_tokens` (16000 by default), `stream: true`, `messages`
/// and `thinking: {"type": "adaptive"}`. When `system` is non-empty, its blocks
/// carry exactly one `cache_control` breakpoint, on the last block. When a
/// schema is present, `output_config.format` is a `json_schema` format.
///
/// Never emits `temperature`, `top_p`, `thinking.budget_tokens` or an assistant
/// prefill; current models reject them.
[[nodiscard]] WireAsk build_ask(const AskRequest& request);

} // namespace leash::drivers::anthropic
