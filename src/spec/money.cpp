// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/spec/money.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>

namespace leash::spec {
namespace {

constexpr std::int64_t kMicrosPerUsd = 1'000'000;
constexpr std::int64_t kFractionScale = 10'000; // four decimal places

std::int64_t clamped_round(long double value) {
    constexpr auto kMin = static_cast<long double>(std::numeric_limits<std::int64_t>::min());
    constexpr auto kMax = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    if (value <= kMin) {
        return std::numeric_limits<std::int64_t>::min();
    }
    if (value >= kMax) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(std::llround(value));
}

} // namespace

Money Money::usd(double value) {
    return Money(clamped_round(static_cast<long double>(value) * kMicrosPerUsd));
}

Money Money::micros(std::int64_t value) { return Money(value); }

std::optional<Money> Money::checked_sub(Money other) const {
    if (other.micros_ > micros_) {
        return std::nullopt;
    }
    return Money(micros_ - other.micros_);
}

std::string Money::display() const {
    const bool negative = micros_ < 0;
    // Avoid negating INT64_MIN.
    const std::uint64_t magnitude = negative ? static_cast<std::uint64_t>(-(micros_ + 1)) + 1U
                                             : static_cast<std::uint64_t>(micros_);
    const std::uint64_t dollars = magnitude / static_cast<std::uint64_t>(kMicrosPerUsd);
    const std::uint64_t fraction =
        (magnitude % static_cast<std::uint64_t>(kMicrosPerUsd)) /
        (static_cast<std::uint64_t>(kMicrosPerUsd) / static_cast<std::uint64_t>(kFractionScale));

    std::ostringstream out;
    if (negative) {
        out << '-';
    }
    out << '$' << dollars << '.' << std::setw(4) << std::setfill('0') << fraction;
    return out.str();
}

std::int64_t Money::micros_value() const noexcept { return micros_; }

} // namespace leash::spec
