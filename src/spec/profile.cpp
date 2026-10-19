// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/spec/profile.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <toml++/toml.hpp>

#include "embedded.hpp"

namespace leash::spec::profiles {
namespace {

struct ArtifactSpec {
    std::string_view key;
    ArtifactKey value;
};

struct ProtocolSpec {
    std::string_view section;
    std::string_view artifact_key;
    ProtocolKey value;
};

constexpr auto kArtifactSpecs = std::to_array<ArtifactSpec>({
    {.key = "tool_registry", .value = ArtifactKey::tool_registry},
    {.key = "test_command_registry", .value = ArtifactKey::test_command_registry},
    {.key = "project_memory", .value = ArtifactKey::memory},
    {.key = "task_state", .value = ArtifactKey::task_state},
    {.key = "check_registry", .value = ArtifactKey::check_registry},
    {.key = "report_template", .value = ArtifactKey::report_template},
});

constexpr auto kProtocolSpecs = std::to_array<ProtocolSpec>({
    {
        .section = "tool_usage",
        .artifact_key = "tool_usage_protocol",
        .value = ProtocolKey::tool_usage,
    },
    {
        .section = "context_selection",
        .artifact_key = "context_selection_protocol",
        .value = ProtocolKey::context_selection,
    },
    {
        .section = "bug_reproduction",
        .artifact_key = "reproduction_protocol",
        .value = ProtocolKey::bug_reproduction,
    },
    {
        .section = "failure_attribution",
        .artifact_key = "attribution_protocol",
        .value = ProtocolKey::failure_attribution,
    },
    {
        .section = "verification",
        .artifact_key = "verification_protocol",
        .value = ProtocolKey::verification,
    },
});

const ArtifactSpec* find_artifact(std::string_view key) {
    const auto* const found = std::ranges::find_if(
        kArtifactSpecs, [key](const ArtifactSpec& spec) { return spec.key == key; });
    return found == kArtifactSpecs.end() ? nullptr : &*found;
}

const ProtocolSpec* find_protocol_by_section(std::string_view section) {
    const auto* const found = std::ranges::find_if(
        kProtocolSpecs, [section](const ProtocolSpec& spec) { return spec.section == section; });
    return found == kProtocolSpecs.end() ? nullptr : &*found;
}

const ProtocolSpec* find_protocol_by_key(std::string_view key) {
    const auto* const found = std::ranges::find_if(
        kProtocolSpecs, [key](const ProtocolSpec& spec) { return spec.artifact_key == key; });
    return found == kProtocolSpecs.end() ? nullptr : &*found;
}

std::expected<bool, SpecError> read_bool(const toml::table& table, std::string_view key,
                                         std::string_view context) {
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return std::unexpected(ParseError{
            .message = std::string(context) + ": missing key '" + std::string(key) + "'",
        });
    }
    const auto value = node->value<bool>();
    if (!value.has_value()) {
        return std::unexpected(ParseError{
            .message = std::string(context) + ": '" + std::string(key) + "' is not a boolean",
        });
    }
    return *value;
}

std::expected<void, SpecError> read_artifacts(const toml::table& artifacts, Profile& profile) {
    for (const auto& spec : kArtifactSpecs) {
        const auto enabled = read_bool(artifacts, spec.key, "[artifacts]");
        if (!enabled.has_value()) {
            return std::unexpected(enabled.error());
        }
        if (*enabled) {
            profile.artifacts.insert(spec.value);
        }
    }

    for (const auto& [key, node] : artifacts) {
        static_cast<void>(node);
        const std::string key_text(key.str());
        if (find_artifact(key_text) == nullptr && find_protocol_by_key(key_text) == nullptr) {
            return std::unexpected(
                ParseError{.message = "[artifacts]: unknown key '" + key_text + "'"});
        }
    }

    return {};
}

std::expected<void, SpecError> read_protocols(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const toml::table& artifacts, const toml::table& system_prompt, Profile& profile) {
    const toml::node* sections_node = system_prompt.get("sections");
    const toml::array* sections = sections_node == nullptr ? nullptr : sections_node->as_array();
    if (sections == nullptr) {
        return std::unexpected(
            ParseError{.message = "profile: missing or non-array [system_prompt].sections"});
    }

    for (const auto& node : *sections) {
        const auto section = node.value<std::string>();
        if (!section.has_value()) {
            return std::unexpected(
                ParseError{.message = "[system_prompt].sections: entry is not a string"});
        }

        const ProtocolSpec* spec = find_protocol_by_section(*section);
        if (spec == nullptr) {
            return std::unexpected(ParseError{
                .message = "[system_prompt].sections: unknown section '" + *section + "'",
            });
        }
        if (std::ranges::find(profile.protocols, spec->value) != profile.protocols.end()) {
            return std::unexpected(ParseError{
                .message = "[system_prompt].sections: duplicate section '" + *section + "'",
            });
        }

        const auto enabled = read_bool(artifacts, spec->artifact_key, "[artifacts]");
        if (!enabled.has_value()) {
            return std::unexpected(enabled.error());
        }
        if (!*enabled) {
            return std::unexpected(ParseError{
                .message = "[system_prompt].sections lists '" + *section +
                           "' but its protocol artifact is disabled",
            });
        }
        profile.protocols.push_back(spec->value);
    }

    for (const auto& spec : kProtocolSpecs) {
        const auto enabled = read_bool(artifacts, spec.artifact_key, "[artifacts]");
        if (!enabled.has_value()) {
            return std::unexpected(enabled.error());
        }
        if (*enabled &&
            std::ranges::find(profile.protocols, spec.value) == profile.protocols.end()) {
            return std::unexpected(ParseError{
                .message = "[artifacts] enables '" + std::string(spec.artifact_key) +
                           "' but [system_prompt].sections omits '" + std::string(spec.section) +
                           "'",
            });
        }
    }

    return {};
}

struct ProfileSource {
    std::string_view text;
    std::string_view expected_name;
};

std::expected<Profile, SpecError> parse(const ProfileSource& source) {
    toml::table root;
    try {
        root = toml::parse(source.text);
    } catch (const toml::parse_error& error) {
        return std::unexpected(ParseError{.message = std::string(error.what())});
    }

    const toml::node* profile_node = root.get("profile");
    const toml::table* profile_table = profile_node == nullptr ? nullptr : profile_node->as_table();
    if (profile_table == nullptr) {
        return std::unexpected(ParseError{.message = "profile: missing [profile] table"});
    }

    const toml::node* name_node = profile_table->get("name");
    const auto name = name_node == nullptr ? std::nullopt : name_node->value<std::string>();
    if (!name.has_value()) {
        return std::unexpected(ParseError{.message = "profile: missing or non-string name"});
    }
    if (*name != source.expected_name) {
        return std::unexpected(ParseError{
            .message = "profile name '" + *name + "' does not match requested name '" +
                       std::string(source.expected_name) + "'",
        });
    }

    const toml::node* artifacts_node = root.get("artifacts");
    const toml::table* artifacts = artifacts_node == nullptr ? nullptr : artifacts_node->as_table();
    if (artifacts == nullptr) {
        return std::unexpected(ParseError{.message = "profile: missing [artifacts] table"});
    }

    const toml::node* system_prompt_node = root.get("system_prompt");
    const toml::table* system_prompt =
        system_prompt_node == nullptr ? nullptr : system_prompt_node->as_table();
    if (system_prompt == nullptr) {
        return std::unexpected(ParseError{.message = "profile: missing [system_prompt] table"});
    }

    Profile profile;
    profile.name = *name;

    if (auto error = read_artifacts(*artifacts, profile); !error.has_value()) {
        return std::unexpected(std::move(error.error()));
    }
    if (auto error = read_protocols(*artifacts, *system_prompt, profile); !error.has_value()) {
        return std::unexpected(std::move(error.error()));
    }

    return profile;
}

std::expected<std::string, SpecError> read_file(const std::string& path,
                                                std::string_view profile_name) {
    std::ifstream input(path);
    if (!input) {
        return std::unexpected(UnknownProfileError{.name = std::string(profile_name)});
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

} // namespace

std::size_t exposed_count(const Profile& profile) noexcept {
    return profile.artifacts.size() + profile.protocols.size();
}

std::expected<Profile, SpecError> load(std::string_view name) {
    const std::string requested(name);

    if (requested == "h0" || requested == "h1" || requested == "h2" || requested == "h3") {
        const std::string path = "spec/profiles/" + requested + ".toml";
        const auto text = detail::embedded_file(path);
        if (!text.has_value()) {
            return std::unexpected(UnknownProfileError{.name = requested});
        }
        return parse(ProfileSource{.text = *text, .expected_name = requested});
    }

    const std::string path = requested + ".toml";
    auto text = read_file(path, requested);
    if (!text.has_value()) {
        return std::unexpected(std::move(text.error()));
    }
    return parse(ProfileSource{.text = *text, .expected_name = requested});
}

} // namespace leash::spec::profiles
