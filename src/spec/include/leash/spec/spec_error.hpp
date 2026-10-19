// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <string>
#include <variant>

namespace leash::spec {

/// A value did not match a JSON Schema. `json_pointer` is the RFC 6901 pointer
/// of the first failure, for example `/usage/input_tokens`; it is empty when
/// the failure is at the document root.
struct SchemaError {
    std::string message;
    std::string json_pointer;
};

/// A document or TOML file could not be parsed at all.
struct ParseError {
    std::string message;
};

/// A requested profile is neither embedded nor readable from the current
/// directory.
struct UnknownProfileError {
    std::string name;
};

using SpecError = std::variant<SchemaError, ParseError, UnknownProfileError>;

/// Human-readable rendering of a SpecError, for diagnostics and tests.
[[nodiscard]] std::string describe(const SpecError& error);

} // namespace leash::spec
