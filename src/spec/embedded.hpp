// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <optional>
#include <string_view>

namespace leash::spec::detail {

/// Text of a file embedded at build time, keyed by its path relative to the
/// repository root (for example "spec/journal.schema.json" or
/// "std/prices.toml"). Returns nullopt for an unknown path.
[[nodiscard]] std::optional<std::string_view> embedded_file(std::string_view path);

} // namespace leash::spec::detail
