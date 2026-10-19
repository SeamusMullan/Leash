// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "leash/syntax/ast.hpp"
#include "leash/syntax/diagnostic.hpp"
#include "leash/syntax/span.hpp"

namespace leash::syntax {

struct Parsed {
    std::optional<ast::Program> program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const { return diagnostics.empty(); }
};

/// Lex and parse one file. Parsing recovers at item and statement boundaries,
/// so a file with two independent errors yields two diagnostics. `program` is
/// present even when diagnostics were produced, unless the file is empty.
///
/// The AST stores `std::string_view`s into `src`; `src` must outlive it.
[[nodiscard]] Parsed parse_program(std::string_view src, FileId file);

} // namespace leash::syntax
