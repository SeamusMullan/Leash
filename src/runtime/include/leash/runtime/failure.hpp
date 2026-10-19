// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "leash/spec/failure.hpp"

namespace leash::runtime {

/// A source position attached to a runtime failure. WP-10 replaces the
/// syntactic `node_kind`/`line` pair with the structural `SiteId`; until then
/// this is enough to point a user at the failing construct.
struct Site {
    std::string module;
    std::string item;
    std::string node_kind;
    std::uint32_t line = 0;

    [[nodiscard]] std::string to_string() const;
    bool operator==(const Site&) const = default;
};

/// A runtime failure, as declared by docs/subagents/shared-interfaces.md.
/// `cause` chains retries and re-raises.
struct Failure {
    spec::FailureKind kind = spec::FailureKind::Unknown;
    std::string message;
    std::optional<nlohmann::json> data;
    Site site;
    std::unique_ptr<Failure> cause;
};

/// The non-local `fail` outcome. `Control` in eval.hpp pairs this with
/// `Complete`; v0 has no `break` or `return`.
struct Fail {
    Failure failure;
};

} // namespace leash::runtime
