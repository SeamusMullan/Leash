// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
#include "leash/cli/cmd/check.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "leash/cli/exit.hpp"
#include "leash/syntax/diagnostic.hpp"
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

} // namespace

int run_syntax_check(const std::vector<std::string>& files, CommandIo output) {
    bool had_error = false;
    std::uint32_t file_id = 1;

    for (const std::string& path : files) {
        std::string source;
        if (!read_file(path, source)) {
            *output.err << "leash: cannot read '" << path << "'\n";
            return to_exit_code(ExitCode::failure);
        }

        const leash::syntax::Parsed parsed =
            leash::syntax::parse_program(source, leash::syntax::FileId{file_id++});
        if (parsed.diagnostics.empty()) {
            continue;
        }

        had_error = true;
        for (const leash::syntax::Diagnostic& diagnostic : parsed.diagnostics) {
            leash::syntax::render(diagnostic, source, *output.err, path);
        }
    }

    if (!had_error) {
        return to_exit_code(ExitCode::ok);
    }

    const std::size_t count = files.size();
    *output.err << (count == 1 ? "1 file" : std::to_string(count) + " files")
                << " checked with errors\n";
    return to_exit_code(ExitCode::usage);
}

} // namespace leash::cli
