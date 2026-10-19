// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <string_view>

namespace leash::drivers {

/// Driver interface and implementations, and the MCP client.
[[nodiscard]] std::string_view module_name();

} // namespace leash::drivers
