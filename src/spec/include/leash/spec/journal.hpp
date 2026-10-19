// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <cstdint>
#include <expected>
#include <string_view>

#include "leash/spec/spec_error.hpp"

namespace leash::spec::journal {

/// Journal format version written into every manifest and checked by replay.
inline constexpr std::uint32_t kVersion = 1;

/// Validate one line of journal.jsonl against spec/journal.schema.json.
///
/// The line may omit its trailing newline. Invalid JSON is a ParseError; a
/// well-formed object that violates the schema is a SchemaError whose
/// json_pointer names the first failing field.
[[nodiscard]] std::expected<void, SpecError> validate_line(std::string_view line);

} // namespace leash::spec::journal
