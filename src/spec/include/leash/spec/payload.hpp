// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <variant>

namespace leash::spec {

/// A reference to a payload produced by a driver or an effect. Small values
/// travel inline; large ones live in the content-addressed store and are
/// named by their BLAKE3 hash ("b3:<hex>").
struct PayloadRef {
    struct Inline {
        nlohmann::json value; // NOLINT(cppcoreguidelines-pro-type-member-init)
    };
    struct Cas {
        std::string hash; // NOLINT(cppcoreguidelines-pro-type-member-init)
    };

    std::variant<Inline, Cas> value;
};

/// The single visitor for PayloadRef. It renders the reference as text: the
/// serialised inline value, or the CAS hash. Call sites that need to inspect
/// the alternative use std::holds_alternative/std::get directly.
struct PayloadRefVisitor {
    [[nodiscard]] std::string operator()(const PayloadRef::Inline& inlined) const {
        return inlined.value.dump();
    }

    [[nodiscard]] std::string operator()(const PayloadRef::Cas& cas) const { return cas.hash; }
};

} // namespace leash::spec
