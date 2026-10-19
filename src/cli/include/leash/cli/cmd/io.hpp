// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
#pragma once

#include <iosfwd>

namespace leash::cli {

/// Output streams for a subcommand. Grouping them avoids a pair of adjacent
/// `std::ostream&` parameters that callers can silently swap. Pointers keep
/// the aggregate assignable and avoid reference data members.
struct CommandIo {
    std::ostream* out = nullptr;
    std::ostream* err = nullptr;
};

} // namespace leash::cli
