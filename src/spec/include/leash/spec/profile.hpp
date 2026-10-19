// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <cstdint>
#include <expected>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "leash/spec/spec_error.hpp"

namespace leash::spec::profiles {

/// Harness artifacts a profile can expose, in canonical order.
enum class ArtifactKey : std::uint8_t {
    tool_registry,
    test_command_registry,
    memory,
    task_state,
    check_registry,
    report_template,
};

/// Harness protocols a profile can expose, in canonical order.
enum class ProtocolKey : std::uint8_t {
    tool_usage,
    context_selection,
    bug_reproduction,
    failure_attribution,
    verification,
};

struct Profile {
    std::string name;
    std::set<ArtifactKey> artifacts; // canonical order
    std::vector<ProtocolKey> protocols;
};

/// Total number of exposed artifacts and protocols, 0..11.
[[nodiscard]] std::size_t exposed_count(const Profile& profile) noexcept;

/// Load an embedded profile h0..h3, or `<name>.toml` from the current
/// directory for any other name.
[[nodiscard]] std::expected<Profile, SpecError> load(std::string_view name);

} // namespace leash::spec::profiles
