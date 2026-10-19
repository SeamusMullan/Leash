// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
#include <CLI/CLI.hpp>

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "leash/cli/cmd/check.hpp"
#include "leash/cli/cmd/fmt.hpp"
#include "leash/cli/doctor.hpp"
#include "leash/cli/exit.hpp"

int main(int argc, char** argv) {
    try {
        CLI::App app{"Leash: deterministic agent workflows"};
        app.set_version_flag("--version,-V", "leash 0.1.0");

        auto* doctor = app.add_subcommand("doctor", "check drivers and prerequisites");
        doctor->callback([] { std::cout << leash::cli::collect_probes().render(); });

        std::vector<std::string> check_files;
        bool syntax_only = false;
        auto* check = app.add_subcommand("check", "check source files");
        check->add_option("files", check_files, "files to check")->required();
        check->add_flag("--syntax", syntax_only,
                        "parse only: report syntax diagnostics (the only check in WP-03)");

        std::vector<std::string> format_files;
        bool check_only = false;
        auto* format = app.add_subcommand("fmt", "format source files in place");
        format->add_option("files", format_files, "files to format")->required();
        format->add_flag("--check", check_only, "report files that are not formatted");

        auto* unimplemented = app.add_subcommand("__unimplemented");
        unimplemented->group("");

        app.require_subcommand(0, 1);
        if (argc == 1) {
            std::cout << app.help();
            return leash::cli::to_exit_code(leash::cli::ExitCode::ok);
        }

        CLI11_PARSE(app, argc, argv);

        if (check->parsed()) {
            if (!syntax_only) {
                std::cerr << "leash: `check` requires --syntax in this version\n";
                return leash::cli::to_exit_code(leash::cli::ExitCode::usage);
            }
            return leash::cli::run_syntax_check(
                check_files, leash::cli::CommandIo{.out = &std::cout, .err = &std::cerr});
        }
        if (format->parsed()) {
            return leash::cli::run_format(
                format_files, check_only,
                leash::cli::CommandIo{.out = &std::cout, .err = &std::cerr});
        }
        return leash::cli::to_exit_code(leash::cli::ExitCode::ok);
    } catch (const std::exception& error) {
        std::cerr << "leash: " << error.what() << '\n';
        return leash::cli::to_exit_code(leash::cli::ExitCode::failure);
    }
}
