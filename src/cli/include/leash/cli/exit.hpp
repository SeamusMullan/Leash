// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
#pragma once

#include <cstdint>

namespace leash::cli {

/// Exit codes from docs/10 §1.
enum class ExitCode : std::uint8_t {
    ok = 0,
    failure = 1,
    usage = 2,
    replay_divergence = 3,
    budget_or_permission = 4,
    interrupted = 130,
};

[[nodiscard]] constexpr int to_exit_code(ExitCode code) noexcept { return static_cast<int>(code); }

} // namespace leash::cli
