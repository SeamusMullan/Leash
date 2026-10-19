// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <string_view>

namespace leash::ir {

/// LIR model, AST to LIR lowering, JSON serialisation and hashing.
[[nodiscard]] std::string_view module_name();

} // namespace leash::ir
