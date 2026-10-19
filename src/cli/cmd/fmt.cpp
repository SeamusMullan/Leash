// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
#include "leash/cli/cmd/fmt.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "leash/cli/exit.hpp"
#include "leash/syntax/diagnostic.hpp"
#include "leash/syntax/formatter.hpp"
#include "leash/syntax/parser.hpp"
#include "leash/syntax/span.hpp"

namespace leash::cli {
namespace {

[[nodiscard]] bool read_file(const std::string& path, std::string& out) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    out = buffer.str();
    return true;
}

/// Write through a sibling temporary and rename so an interrupted run never
/// truncates the user's file.
[[nodiscard]] bool write_file_atomically(const std::string& path, std::string_view contents) {
    const std::string temporary = path + ".leash-fmt.tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return false;
        }
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!stream) {
            return false;
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}

} // namespace

int run_format(const std::vector<std::string>& files, bool check, CommandIo output) {
    bool parse_error = false;
    bool differs = false;
    std::uint32_t file_id = 1;

    for (const std::string& path : files) {
        std::string source;
        if (!read_file(path, source)) {
            *output.err << "leash: cannot read '" << path << "'\n";
            return to_exit_code(ExitCode::failure);
        }

        const leash::syntax::Parsed parsed =
            leash::syntax::parse_program(source, leash::syntax::FileId{file_id++});
        if (!parsed.diagnostics.empty()) {
            parse_error = true;
            for (const leash::syntax::Diagnostic& diagnostic : parsed.diagnostics) {
                leash::syntax::render(diagnostic, source, *output.err, path);
            }
            continue;
        }
        if (!parsed.program.has_value()) {
            continue;
        }

        const std::string formatted = leash::syntax::format_program(*parsed.program, source);
        if (check) {
            if (formatted != source) {
                differs = true;
                *output.out << path << " is not formatted\n";
            }
            continue;
        }
        if (formatted == source) {
            continue;
        }
        if (!write_file_atomically(path, formatted)) {
            *output.err << "leash: cannot write '" << path << "'\n";
            return to_exit_code(ExitCode::failure);
        }
    }

    if (parse_error) {
        return to_exit_code(ExitCode::usage);
    }
    return differs ? to_exit_code(ExitCode::failure) : to_exit_code(ExitCode::ok);
}

} // namespace leash::cli
