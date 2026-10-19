// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <string_view>

namespace leash::store {

/// Content-addressed store, run directory and manifest.
[[nodiscard]] std::string_view module_name();

} // namespace leash::store
