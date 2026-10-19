// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/spec/prices.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <toml++/toml.hpp>

#include "embedded.hpp"

namespace leash::spec::prices {
namespace {

constexpr double kCacheReadMultiplier = 0.1;
constexpr double kCacheWriteMultiplier = 1.25;

struct PriceFields {
    std::optional<double> input;
    std::optional<double> output;
    std::optional<double> cache_read;
    std::optional<double> cache_write;
};

std::optional<double> numeric_value(const toml::node& node) {
    if (const auto floating = node.value<double>(); floating.has_value()) {
        return floating;
    }
    if (const auto integer = node.value<std::int64_t>(); integer.has_value()) {
        return static_cast<double>(*integer);
    }
    return std::nullopt;
}

std::optional<SpecError> read_field(const toml::table& entry, std::string_view field,
                                    const std::string& model, std::optional<double>& destination) {
    const toml::node* node = entry.get(field);
    if (node == nullptr) {
        return std::nullopt;
    }

    const auto value = numeric_value(*node);
    if (!value.has_value()) {
        return ParseError{
            .message = "model '" + model + "': '" + std::string(field) + "' is not a number",
        };
    }
    if (!std::isfinite(*value) || *value < 0.0) {
        return ParseError{
            .message = "model '" + model + "': '" + std::string(field) +
                       "' must be finite and non-negative",
        };
    }

    destination = value;
    return std::nullopt;
}

std::expected<PriceFields, SpecError> read_fields(const toml::table& entry,
                                                  const std::string& model) {
    PriceFields fields;
    const std::array<std::pair<std::string_view, std::optional<double>*>, 4> slots{
        {
            {"input", &fields.input},
            {"output", &fields.output},
            {"cache_read", &fields.cache_read},
            {"cache_write", &fields.cache_write},
        },
    };

    for (const auto& [name, destination] : slots) {
        if (auto error = read_field(entry, name, model, *destination)) {
            return std::unexpected(std::move(*error));
        }
    }
    return fields;
}

void apply_fields(ModelPrice& price, const PriceFields& fields, bool is_new) {
    if (fields.input.has_value()) {
        price.input = *fields.input;
    }
    if (fields.output.has_value()) {
        price.output = *fields.output;
    }

    if (fields.cache_read.has_value()) {
        price.cache_read = *fields.cache_read;
    } else if (is_new || fields.input.has_value()) {
        price.cache_read = price.input * kCacheReadMultiplier;
    }

    if (fields.cache_write.has_value()) {
        price.cache_write = *fields.cache_write;
    } else if (is_new || fields.input.has_value()) {
        price.cache_write = price.input * kCacheWriteMultiplier;
    }
}

} // namespace

PriceTable PriceTable::default_embedded() {
    const auto text = detail::embedded_file("std/prices.toml");
    if (!text.has_value()) {
        return PriceTable{};
    }
    auto parsed = parse(*text, nullptr);
    if (!parsed.has_value()) {
        return PriceTable{};
    }
    return std::move(*parsed);
}

std::expected<PriceTable, SpecError> PriceTable::parse(std::string_view toml,
                                                       const PriceTable* base) {
    toml::table root;
    try {
        root = toml::parse(toml);
    } catch (const toml::parse_error& error) {
        return std::unexpected(ParseError{.message = std::string(error.what())});
    }

    const toml::node* models_node = root.get("models");
    const toml::table* models = models_node == nullptr ? nullptr : models_node->as_table();
    if (models == nullptr) {
        if (base != nullptr) {
            return *base;
        }
        return std::unexpected(ParseError{.message = "price table: missing [models] table"});
    }

    PriceTable result;
    if (base != nullptr) {
        result = *base;
    }

    for (const auto& [key, node] : *models) {
        const toml::table* entry = node.as_table();
        if (entry == nullptr) {
            return std::unexpected(
                ParseError{.message = "models." + std::string(key.str()) + " is not a table"});
        }

        const std::string model(key.str());
        auto fields = read_fields(*entry, model);
        if (!fields.has_value()) {
            return std::unexpected(std::move(fields.error()));
        }

        const auto existing = result.prices_.find(model);
        const bool is_new = existing == result.prices_.end();
        if (is_new && !fields->input.has_value()) {
            return std::unexpected(ParseError{
                .message = "model '" + model + "': missing 'input'",
            });
        }
        if (is_new && !fields->output.has_value()) {
            return std::unexpected(ParseError{
                .message = "model '" + model + "': missing 'output'",
            });
        }

        ModelPrice price = is_new ? ModelPrice{} : existing->second;
        apply_fields(price, *fields, is_new);
        result.prices_.insert_or_assign(model, price);
    }

    if (result.prices_.empty()) {
        return std::unexpected(ParseError{.message = "price table: no models"});
    }
    return result;
}

std::expected<PriceTable, SpecError> PriceTable::with_overrides(std::string_view toml) const {
    return parse(toml, this);
}

std::optional<ModelPrice> PriceTable::price_for(std::string_view model) const {
    const auto found = prices_.find(model);
    if (found == prices_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<Money> PriceTable::cost_usd(std::string_view model, const Usage& usage) const {
    const auto price = price_for(model);
    if (!price.has_value()) {
        return std::nullopt;
    }
    if (usage.cost.has_value()) {
        return usage.cost;
    }

    const long double micros =
        (static_cast<long double>(usage.input_tokens) * price->input) +
        (static_cast<long double>(usage.output_tokens) * price->output) +
        (static_cast<long double>(usage.cache_read_input_tokens) * price->cache_read) +
        (static_cast<long double>(usage.cache_creation_input_tokens) * price->cache_write);
    return Money::micros(std::llround(micros));
}

} // namespace leash::spec::prices
