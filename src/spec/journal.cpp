// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/spec/journal.hpp"

#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "validation.hpp"

namespace leash::spec::journal {

std::expected<void, SpecError> validate_line(std::string_view line) {
    if (line.empty()) {
        return std::unexpected(ParseError{.message = "empty journal line"});
    }

    nlohmann::json value;
    try {
        value = nlohmann::json::parse(line);
    } catch (const nlohmann::json::parse_error& error) {
        return std::unexpected(ParseError{.message = error.what()});
    }

    if (!value.is_object()) {
        return std::unexpected(SchemaError{
            .message = "journal entry must be a JSON object",
            .json_pointer = "",
        });
    }

    if (auto error = detail::validate(detail::journal_schema(), value)) {
        return std::unexpected(std::move(*error));
    }
    return {};
}

} // namespace leash::spec::journal
