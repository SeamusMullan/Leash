// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <array>
#include <string_view>

namespace leash::spec {

/// The eleven failure kinds of docs/02-language-spec.md §9.1.
enum class FailureKind : unsigned char {
    Context,
    Tool,
    Feedback,
    Verify,
    Recovery,
    Entropy,
    Model,
    Budget,
    Permission,
    Replay,
    Unknown,
};

/// Source spelling of a failure kind, for `fail Kind(...)` and diagnostics.
[[nodiscard]] constexpr std::string_view to_string(FailureKind kind) {
    constexpr std::array<std::string_view, 11> names = {
        "Context", "Tool",   "Feedback",   "Verify", "Recovery", "Entropy",
        "Model",   "Budget", "Permission", "Replay", "Unknown",
    };
    return names.at(static_cast<std::size_t>(kind));
}

/// Parse the source spelling. Returns `Unknown` for an unrecognised name; the
/// caller decides whether that is an error.
[[nodiscard]] constexpr FailureKind failure_kind_from_string(std::string_view name) {
    constexpr std::array<FailureKind, 11> kinds = {
        FailureKind::Context,    FailureKind::Tool,    FailureKind::Feedback, FailureKind::Verify,
        FailureKind::Recovery,   FailureKind::Entropy, FailureKind::Model,    FailureKind::Budget,
        FailureKind::Permission, FailureKind::Replay,  FailureKind::Unknown,
    };
    for (const FailureKind kind : kinds) {
        if (to_string(kind) == name) {
            return kind;
        }
    }
    return FailureKind::Unknown;
}

} // namespace leash::spec
