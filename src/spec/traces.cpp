// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/spec/traces.hpp"

#include <utility>

#include "validation.hpp"

namespace leash::spec::traces {

std::expected<void, SpecError> validate(TraceKind kind, const nlohmann::json& value) {
    if (auto error = detail::validate(detail::trace_schema(kind), value)) {
        return std::unexpected(std::move(*error));
    }
    return {};
}

} // namespace leash::spec::traces
