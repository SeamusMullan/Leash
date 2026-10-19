// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <string>
#include <string_view>

#include "leash/syntax/ast.hpp"

namespace leash::syntax {

/// Pretty-print a parsed program.
///
/// Guarantees, for input that parsed without diagnostics:
///   * two-space indentation, trailing commas in multi-line literals;
///   * `format_program(format_program(x))` is identical to `format_program(x)`
///     (idempotent);
///   * re-parsing the output produces a structurally equal AST (comments are
///     preserved, so doc comments survive too).
///
/// `src` is the buffer the AST slices; it is accepted for symmetry with the
/// parser and so a future implementation can recover verbatim spans.
[[nodiscard]] std::string format_program(const ast::Program& program, std::string_view src);

} // namespace leash::syntax
