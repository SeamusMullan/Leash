// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <string_view>

namespace leash::lsp {

/// Language server.
[[nodiscard]] std::string_view module_name();

} // namespace leash::lsp
