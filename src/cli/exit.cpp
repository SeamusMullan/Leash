// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
#include "leash/cli/exit.hpp"

namespace leash::cli {

// The mapping is intentionally header-only; this translation unit anchors the
// module and gives the test target something to link.
[[maybe_unused]] constexpr int kOk = to_exit_code(ExitCode::ok);

} // namespace leash::cli
