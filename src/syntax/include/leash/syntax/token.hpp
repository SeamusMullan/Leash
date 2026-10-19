// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "leash/syntax/span.hpp"

namespace leash::syntax {

enum class TokenKind : std::uint8_t {
    Eof,
    Newline,
    Error,

    Identifier,
    IntLit,
    FloatLit,
    DurationLit,

    // Interpolated strings are lexed as a token sequence:
    //   StringStart StringPart? (InterpStart <expr tokens> InterpEnd StringPart?)* StringEnd
    // Raw strings are StringStart StringPart? StringEnd. `{{` and `}}` stay in
    // StringPart verbatim so the formatter can reproduce the source exactly.
    StringStart,
    StringPart,
    InterpStart,
    InterpEnd,
    StringEnd,

    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Semicolon,
    Colon,
    Dot,
    DotDot,
    Question,
    Arrow,
    FatArrow,
    Eq,
    EqEq,
    NotEq,
    Lt,
    Le,
    Gt,
    Ge,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Bang,
    Pipe,

    KwModule,
    KwUse,
    KwPub,
    KwLet,
    KwVar,
    KwFn,
    KwType,
    KwEnum,
    KwTool,
    KwAgent,
    KwMemory,
    KwTask,
    KwFlow,
    KwAsk,
    KwRun,
    KwSpawn,
    KwAwait,
    KwParallel,
    KwRace,
    KwRetry,
    KwAttempt,
    KwRecover,
    KwFail,
    KwCheckpoint,
    KwVerify,
    KwComplete,
    KwIf,
    KwElse,
    KwMatch,
    KwFor,
    KwIn,
    KwWhile,
    KwLoop,
    KwBreak,
    KwContinue,
    KwReturn,
    KwTrue,
    KwFalse,
    KwNull,
    KwAnd,
    KwOr,
    KwNot,
    KwAs,
    KwWhere,
    KwBudget,
    KwAllow,

    Count,
};

enum class TriviaKind : std::uint8_t { LineComment, BlockComment, DocComment };

/// A comment (or a run of doc comments) attached to the token that follows it.
/// `text` is a slice of the source buffer, without the trailing newline for
/// line and doc comments and including the delimiters (`//`, `/*`, `///`).
struct Trivia {
    TriviaKind kind = TriviaKind::LineComment;
    Span span;
    std::string_view text;

    bool operator==(const Trivia&) const = default;
};

struct Token {
    TokenKind kind = TokenKind::Eof;
    Span span;
    std::string_view text;
    /// Comments between the previous token and this one.
    std::vector<Trivia> leading;
    /// True when at least one blank line separated this token from the
    /// previous non-trivia token.
    bool blank_line_before = false;

    [[nodiscard]] bool is(TokenKind expected) const { return kind == expected; }
};

[[nodiscard]] std::string_view token_kind_name(TokenKind kind);

/// True for the reserved words of `spec/grammar.ebnf` §"Lexical". Contextual
/// keywords (`input`, `output`, `ensure`, ...) remain identifiers.
[[nodiscard]] bool is_keyword(TokenKind kind);

[[nodiscard]] bool is_comment(TriviaKind kind);

} // namespace leash::syntax
