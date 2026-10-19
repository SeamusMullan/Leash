// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <expected>

#include <nlohmann/json.hpp>

#include "leash/spec/spec_error.hpp"

namespace leash::spec::manifest {

/// Validate a parsed manifest.json against spec/manifest.schema.json.
[[nodiscard]] std::expected<void, SpecError> validate(const nlohmann::json& value);

} // namespace leash::spec::manifest
