// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
#include <CLI/CLI.hpp>

#include <exception>
#include <iostream>

#include "leash/cli/doctor.hpp"

int main(int argc, char** argv) {
    try {
        CLI::App app{"Leash: deterministic agent workflows"};
        app.set_version_flag("--version,-V", "leash 0.1.0");

        auto* doctor = app.add_subcommand("doctor", "check drivers and prerequisites");
        doctor->callback([] { std::cout << leash::cli::collect_probes().render(); });

        auto* unimplemented = app.add_subcommand("__unimplemented");
        unimplemented->group("");

        app.require_subcommand(0, 1);
        if (argc == 1) {
            std::cout << app.help();
            return 0;
        }

        CLI11_PARSE(app, argc, argv);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "leash: " << error.what() << '\n';
        return 1;
    }
}
