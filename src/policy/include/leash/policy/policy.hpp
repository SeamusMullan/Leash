// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <string_view>

namespace leash::policy {

/// Shell tokenisation and command matching, path policy and host matching.
[[nodiscard]] std::string_view module_name();

} // namespace leash::policy
