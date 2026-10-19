// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
#pragma once

#include <string>
#include <vector>

#include "leash/cli/cmd/io.hpp"

namespace leash::cli {

/// Parse each file and render every syntax diagnostic.
///
/// Prints nothing when all files parse cleanly. Returns `ExitCode::usage`
/// (2) when any file has a diagnostic and `ExitCode::failure` (1) when a file
/// cannot be read. `--syntax` is the only check implemented in WP-03; later
/// work packages add type checking behind the same subcommand.
[[nodiscard]] int run_syntax_check(const std::vector<std::string>& files, CommandIo output);

} // namespace leash::cli
