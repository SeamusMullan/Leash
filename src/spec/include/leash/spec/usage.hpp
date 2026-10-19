// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "leash/spec/money.hpp"

namespace leash::spec {

/// What a driver reports for one turn or one call. Counters that are consumed
/// rather than reported (tool_calls, llm_calls, turns) belong to the runtime's
/// budget accounting, not here.
struct Usage {
    std::uint64_t input_tokens = 0;
    std::uint64_t output_tokens = 0;
    std::uint64_t cache_read_input_tokens = 0;
    std::uint64_t cache_creation_input_tokens = 0;

    /// Set when the backend reports an exact cost (Claude Code's
    /// `total_cost_usd`); otherwise the price table computes it.
    std::optional<Money> cost;

    std::string model;

    [[nodiscard]] std::uint64_t total_tokens() const noexcept;
};

} // namespace leash::spec
