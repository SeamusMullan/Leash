// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/spec/manifest.hpp"

#include <utility>

#include "validation.hpp"

namespace leash::spec::manifest {

std::expected<void, SpecError> validate(const nlohmann::json& value) {
    if (auto error = detail::validate(detail::manifest_schema(), value)) {
        return std::unexpected(std::move(*error));
    }
    return {};
}

} // namespace leash::spec::manifest
