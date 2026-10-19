// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/spec/spec_error.hpp"

#include <type_traits>
#include <utility>

namespace leash::spec {

std::string describe(const SpecError& error) {
    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SchemaError>) {
                if (value.json_pointer.empty()) {
                    return "schema error: " + value.message;
                }
                return "schema error at " + value.json_pointer + ": " + value.message;
            } else if constexpr (std::is_same_v<T, ParseError>) {
                return "parse error: " + value.message;
            } else {
                return "unknown profile: " + value.name;
            }
        },
        error);
}

} // namespace leash::spec
