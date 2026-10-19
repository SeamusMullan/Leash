// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <cstdint>
#include <expected>

#include <nlohmann/json.hpp>

#include "leash/spec/spec_error.hpp"

namespace leash::spec::lir {

/// LIR format version carried by the envelope and the manifest.
inline constexpr std::uint32_t kVersion = 1;

/// Validate a parsed LIR document against spec/lir.schema.json.
[[nodiscard]] std::expected<void, SpecError> validate(const nlohmann::json& value);

} // namespace leash::spec::lir
