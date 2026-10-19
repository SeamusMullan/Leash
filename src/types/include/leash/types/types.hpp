// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <string_view>

namespace leash::types {

/// Name resolution, type checker, effect inference, capability checker and JSON Schema generation.
[[nodiscard]] std::string_view module_name();

} // namespace leash::types
