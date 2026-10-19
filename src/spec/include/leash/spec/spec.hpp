// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <string_view>

namespace leash::spec {

/// Shared vocabulary: money, usage, budgets, capability sets, failure kinds, payload references,
/// profiles and the price table.
[[nodiscard]] std::string_view module_name();

} // namespace leash::spec
