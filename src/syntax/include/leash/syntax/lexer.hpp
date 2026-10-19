// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <string_view>
#include <vector>

#include "leash/syntax/span.hpp"
#include "leash/syntax/token.hpp"

namespace leash::syntax {

/// Tokenise `src`. The lexer never fails: an unrecognised byte becomes a
/// `TokenKind::Error` token and lexing continues.
///
/// `Token::text` slices `src`; the caller must keep `src` alive for as long as
/// the tokens (or an AST built from them) are used.
[[nodiscard]] std::vector<Token> lex(std::string_view src, FileId file);

} // namespace leash::syntax
