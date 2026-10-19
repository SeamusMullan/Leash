// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "leash/spec/money.hpp"
#include "leash/spec/spec_error.hpp"
#include "leash/spec/usage.hpp"

namespace leash::spec::prices {

/// USD per million tokens for one model.
struct ModelPrice {
    double input = 0.0;
    double output = 0.0;
    double cache_read = 0.0;
    double cache_write = 0.0;
};

class PriceTable {
  public:
    /// The price table embedded from std/prices.toml at build time.
    static PriceTable default_embedded();

    /// Parse a `[models."<id>"]` TOML snippet and merge it over this table.
    /// Fields that are absent keep their current value (or the documented
    /// default of 0.1x input for cache reads and 1.25x input for cache
    /// writes when the model is new).
    [[nodiscard]] std::expected<PriceTable, SpecError> with_overrides(std::string_view toml) const;

    [[nodiscard]] std::optional<ModelPrice> price_for(std::string_view model) const;

    /// Exact cost of one usage record. Returns nullopt for an unknown model.
    /// A backend-reported Usage::cost wins over the table when the model is
    /// known.
    [[nodiscard]] std::optional<Money> cost_usd(std::string_view model, const Usage& usage) const;

  private:
    std::map<std::string, ModelPrice, std::less<>> prices_;

    static std::expected<PriceTable, SpecError> parse(std::string_view toml,
                                                      const PriceTable* base);
};

} // namespace leash::spec::prices
