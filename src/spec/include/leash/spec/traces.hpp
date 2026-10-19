// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <cstdint>
#include <expected>

#include <nlohmann/json.hpp>

#include "leash/spec/spec_error.hpp"

namespace leash::spec::traces {

/// The eight episode trace files of docs/11 §1.
enum class TraceKind : std::uint8_t {
    action,
    tool,
    context,
    verification,
    attribution,
    intervention,
    entropy,
    outcome,
};

/// Validate one trace entry against spec/traces/<kind>.schema.json.
[[nodiscard]] std::expected<void, SpecError> validate(TraceKind kind, const nlohmann::json& value);

} // namespace leash::spec::traces
