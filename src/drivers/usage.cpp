// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/drivers/anthropic/usage.hpp"

#include "leash/drivers/anthropic/consts.hpp"

#include <cstdint>
#include <string>

namespace leash::drivers::anthropic {

namespace {

[[nodiscard]] bool has_number(const nlohmann::json& object, std::string_view key) {
    const auto entry = object.find(std::string(key));
    return entry != object.end() && entry->is_number();
}

[[nodiscard]] std::uint64_t read_number(const nlohmann::json& object, std::string_view key) {
    const auto& value = object.at(std::string(key));
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    const auto signed_value = value.get<std::int64_t>();
    return signed_value < 0 ? 0U : static_cast<std::uint64_t>(signed_value);
}

void apply_counter(const nlohmann::json& object, std::string_view key, std::uint64_t& target) {
    if (has_number(object, key)) {
        target = read_number(object, key);
    }
}

} // namespace

void apply_message_start(Usage& usage, const nlohmann::json& message) {
    if (!message.is_object()) {
        return;
    }

    if (const auto model = message.find(std::string(consts::kFieldModel));
        model != message.end() && model->is_string()) {
        usage.model = model->get<std::string>();
    }

    const auto usage_it = message.find(std::string(consts::kFieldUsage));
    if (usage_it == message.end() || !usage_it->is_object()) {
        return;
    }

    apply_counter(*usage_it, consts::kFieldInputTokens, usage.input_tokens);
    apply_counter(*usage_it, consts::kFieldOutputTokens, usage.output_tokens);
    apply_counter(*usage_it, consts::kFieldCacheReadInputTokens, usage.cache_read_input_tokens);
    apply_counter(*usage_it, consts::kFieldCacheCreationInputTokens,
                  usage.cache_creation_input_tokens);
}

void apply_message_delta(Usage& target, const nlohmann::json& usage) {
    if (!usage.is_object()) {
        return;
    }

    // Each value is the running total for the message. Assign; never add.
    apply_counter(usage, consts::kFieldInputTokens, target.input_tokens);
    apply_counter(usage, consts::kFieldOutputTokens, target.output_tokens);
    apply_counter(usage, consts::kFieldCacheReadInputTokens, target.cache_read_input_tokens);
    apply_counter(usage, consts::kFieldCacheCreationInputTokens,
                  target.cache_creation_input_tokens);
}

} // namespace leash::drivers::anthropic
