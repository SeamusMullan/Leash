// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
#pragma once

#include <string>
#include <vector>

#include "leash/cli/cmd/io.hpp"

namespace leash::cli {

/// Format files in place, or verify them with `--check`.
///
/// Unparseable input renders its diagnostics and returns 2; an unreadable file
/// returns 1. With `--check`, a file that differs from its formatted form is
/// reported and the command returns 1; otherwise it returns 0. Idempotence is
/// the formatter's contract, so a second run always reports clean.
[[nodiscard]] int run_format(const std::vector<std::string>& files, bool check, CommandIo output);

} // namespace leash::cli
