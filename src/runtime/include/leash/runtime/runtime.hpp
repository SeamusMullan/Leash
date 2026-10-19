// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <string_view>

namespace leash::runtime {

/// Interpreter, values, failure handling, scopes, scheduler, journal and replay.
[[nodiscard]] std::string_view module_name();

} // namespace leash::runtime
