// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <optional>
#include <string_view>

#include <nlohmann/json.hpp>
#include <valijson/schema.hpp>

#include "leash/spec/spec_error.hpp"
#include "leash/spec/traces.hpp"

namespace leash::spec::detail {

/// A schema document plus its compiled valijson form. Compilation happens once
/// per schema in a function-local static, which C++ initialises thread-safely.
struct CompiledSchema {
    nlohmann::json document;
    valijson::Schema schema;

    explicit CompiledSchema(std::string_view path);
};

[[nodiscard]] const CompiledSchema& journal_schema();
[[nodiscard]] const CompiledSchema& manifest_schema();
[[nodiscard]] const CompiledSchema& lir_schema();
[[nodiscard]] const CompiledSchema& trace_schema(traces::TraceKind kind);

/// Validate a parsed JSON value. On failure the SchemaError names the first
/// failing RFC 6901 pointer; the path is recorded by walking the schema, not
/// by trusting valijson's error context.
[[nodiscard]] std::optional<SchemaError> validate(const CompiledSchema& compiled,
                                                  const nlohmann::json& value);

} // namespace leash::spec::detail
