// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/spec/lir.hpp"

#include <utility>

#include "validation.hpp"

namespace leash::spec::lir {

std::expected<void, SpecError> validate(const nlohmann::json& value) {
    if (auto error = detail::validate(detail::lir_schema(), value)) {
        return std::unexpected(std::move(*error));
    }
    return {};
}

} // namespace leash::spec::lir
