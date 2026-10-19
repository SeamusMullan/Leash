// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace leash::spec {

/// Exact money, stored as a signed count of micro-USD. Integers avoid the
/// rounding errors that a float budget would accumulate across a run.
class Money {
  public:
    /// Construct from a USD amount, rounding to the nearest micro-USD.
    static Money usd(double value);

    /// Construct from an exact micro-USD count.
    static Money micros(std::int64_t value);

    /// Subtraction that refuses to cross zero.
    [[nodiscard]] std::optional<Money> checked_sub(Money other) const;

    /// Canonical four-decimal rendering, for example "$3.1200".
    [[nodiscard]] std::string display() const;

    /// Exact micro-USD value, for accounting and tests.
    [[nodiscard]] std::int64_t micros_value() const noexcept;

    friend bool operator==(Money lhs, Money rhs) noexcept = default;

  private:
    explicit Money(std::int64_t micros) noexcept : micros_(micros) {}

    std::int64_t micros_ = 0;
};

} // namespace leash::spec
