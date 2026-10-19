// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
#pragma once

#include <string>
#include <vector>

namespace leash::cli {

struct Probe {
    std::string name;
    bool found{false};
    std::string detail;
};

struct DoctorReport {
    std::vector<Probe> probes;

    [[nodiscard]] std::string render() const;
};

/// Probe the local toolchain, drivers and credentials. Never spawns a driver
/// with a prompt: each probe runs `<tool> --version` with a five second timeout.
[[nodiscard]] DoctorReport collect_probes();

} // namespace leash::cli
