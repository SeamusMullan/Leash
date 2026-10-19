// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "leash/syntax/span.hpp"

namespace leash::syntax {

enum class Severity : std::uint8_t { Error, Warning, Note };

[[nodiscard]] std::string_view severity_name(Severity severity);

/// The canonical diagnostic shape shared with `leash_types` and `leash_lsp`.
/// `code` points at a static literal such as "E0102"; diagnostics are copied
/// but never own their code string.
struct Diagnostic {
    std::string_view code; // static literal, e.g. "E0102"
    Severity severity = Severity::Error;
    std::string message;
    std::vector<std::pair<Span, std::string>> labels;
    std::optional<std::string> help;
};

/// Render a diagnostic with a source snippet and one underline per label.
///
/// The format is stable enough for golden tests:
///     error[E0102]: unexpected token
///      --> <input>:3:14
///       |
///     3 |   let x = f(1, 2
///       |              ^
///       = help: ...
void render(const Diagnostic& diagnostic, std::string_view src, std::ostream& out,
            std::string_view filename = "<input>");

/// Convenience overload used by tests and the CLI.
[[nodiscard]] std::string render_to_string(const Diagnostic& diagnostic, std::string_view src,
                                           std::string_view filename = "<input>");

} // namespace leash::syntax
