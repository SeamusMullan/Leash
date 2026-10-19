// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/syntax/lexer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace leash::syntax {
namespace {

constexpr std::array<std::string_view, static_cast<std::size_t>(TokenKind::Count)> kTokenNames = {
    "eof",         "newline",
    "error",       "identifier",
    "int",         "float",
    "duration",    "string_start",
    "string_part", "interp_start",
    "interp_end",  "string_end",
    "(",           ")",
    "{",           "}",
    "[",           "]",
    ",",           ";",
    ":",           ".",
    "..",          "?",
    "->",          "=>",
    "=",           "==",
    "!=",          "<",
    "<=",          ">",
    ">=",          "+",
    "-",           "*",
    "/",           "%",
    "!",           "|",
    "module",      "use",
    "pub",         "let",
    "var",         "fn",
    "type",        "enum",
    "tool",        "agent",
    "memory",      "task",
    "flow",        "ask",
    "run",         "spawn",
    "await",       "parallel",
    "race",        "retry",
    "attempt",     "recover",
    "fail",        "checkpoint",
    "verify",      "complete",
    "if",          "else",
    "match",       "for",
    "in",          "while",
    "loop",        "break",
    "continue",    "return",
    "true",        "false",
    "null",        "and",
    "or",          "not",
    "as",          "where",
    "budget",      "allow",
};

struct Keyword {
    std::string_view spelling;
    TokenKind kind;
};

constexpr std::array<Keyword, 46> kKeywords = {
    {
        {.spelling = "module", .kind = TokenKind::KwModule},
        {.spelling = "use", .kind = TokenKind::KwUse},
        {.spelling = "pub", .kind = TokenKind::KwPub},
        {.spelling = "let", .kind = TokenKind::KwLet},
        {.spelling = "var", .kind = TokenKind::KwVar},
        {.spelling = "fn", .kind = TokenKind::KwFn},
        {.spelling = "type", .kind = TokenKind::KwType},
        {.spelling = "enum", .kind = TokenKind::KwEnum},
        {.spelling = "tool", .kind = TokenKind::KwTool},
        {.spelling = "agent", .kind = TokenKind::KwAgent},
        {.spelling = "memory", .kind = TokenKind::KwMemory},
        {.spelling = "task", .kind = TokenKind::KwTask},
        {.spelling = "flow", .kind = TokenKind::KwFlow},
        {.spelling = "ask", .kind = TokenKind::KwAsk},
        {.spelling = "run", .kind = TokenKind::KwRun},
        {.spelling = "spawn", .kind = TokenKind::KwSpawn},
        {.spelling = "await", .kind = TokenKind::KwAwait},
        {.spelling = "parallel", .kind = TokenKind::KwParallel},
        {.spelling = "race", .kind = TokenKind::KwRace},
        {.spelling = "retry", .kind = TokenKind::KwRetry},
        {.spelling = "attempt", .kind = TokenKind::KwAttempt},
        {.spelling = "recover", .kind = TokenKind::KwRecover},
        {.spelling = "fail", .kind = TokenKind::KwFail},
        {.spelling = "checkpoint", .kind = TokenKind::KwCheckpoint},
        {.spelling = "verify", .kind = TokenKind::KwVerify},
        {.spelling = "complete", .kind = TokenKind::KwComplete},
        {.spelling = "if", .kind = TokenKind::KwIf},
        {.spelling = "else", .kind = TokenKind::KwElse},
        {.spelling = "match", .kind = TokenKind::KwMatch},
        {.spelling = "for", .kind = TokenKind::KwFor},
        {.spelling = "in", .kind = TokenKind::KwIn},
        {.spelling = "while", .kind = TokenKind::KwWhile},
        {.spelling = "loop", .kind = TokenKind::KwLoop},
        {.spelling = "break", .kind = TokenKind::KwBreak},
        {.spelling = "continue", .kind = TokenKind::KwContinue},
        {.spelling = "return", .kind = TokenKind::KwReturn},
        {.spelling = "true", .kind = TokenKind::KwTrue},
        {.spelling = "false", .kind = TokenKind::KwFalse},
        {.spelling = "null", .kind = TokenKind::KwNull},
        {.spelling = "and", .kind = TokenKind::KwAnd},
        {.spelling = "or", .kind = TokenKind::KwOr},
        {.spelling = "not", .kind = TokenKind::KwNot},
        {.spelling = "as", .kind = TokenKind::KwAs},
        {.spelling = "where", .kind = TokenKind::KwWhere},
        {.spelling = "budget", .kind = TokenKind::KwBudget},
        {.spelling = "allow", .kind = TokenKind::KwAllow},
    },
};

[[nodiscard]] constexpr bool is_ident_start(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           character == '_';
}

[[nodiscard]] constexpr bool is_ident_continue(char character) {
    return is_ident_start(character) || (character >= '0' && character <= '9');
}

[[nodiscard]] constexpr bool is_digit(char character) {
    return character >= '0' && character <= '9';
}

[[nodiscard]] constexpr bool is_space(char character) {
    return character == ' ' || character == '\t' || character == '\v' || character == '\f';
}

class Lexer {
  public:
    Lexer(std::string_view source, FileId file) : source_(source), file_(file) {
        modes_.push_back(
            Mode{.kind = ModeKind::Normal, .triple = false, .raw = false, .brace_depth = 0});
    }

    std::vector<Token> run() {
        while (!at_end()) {
            if (modes_.back().kind == ModeKind::String) {
                scan_string_body();
                continue;
            }

            const char character = peek();
            if (scan_trivia(character) || scan_interpolation_brace() || scan_leaf(character)) {
                continue;
            }
            scan_punctuation();
        }

        emit(TokenKind::Eof, position_, position_);
        return std::move(tokens_);
    }

  private:
    /// Whitespace, newlines and comments. Returns true when consumed.
    bool scan_trivia(char character) {
        if (character == '\n' || character == '\r') {
            scan_newline();
            return true;
        }
        if (is_space(character)) {
            ++position_;
            return true;
        }
        if (character == '/' && peek(1) == '/') {
            scan_line_comment();
            return true;
        }
        if (character == '/' && peek(1) == '*') {
            scan_block_comment();
            return true;
        }
        return false;
    }

    /// `{` and `}` while lexing an interpolation. Returns true when consumed.
    bool scan_interpolation_brace() {
        if (modes_.back().kind != ModeKind::Interp) {
            return false;
        }
        const char character = peek();
        if (character == '}') {
            if (modes_.back().brace_depth == 0) {
                emit(TokenKind::InterpEnd, position_, position_ + 1);
                ++position_;
                modes_.pop_back();
            } else {
                --modes_.back().brace_depth;
                emit(TokenKind::RBrace, position_, position_ + 1);
                ++position_;
            }
            return true;
        }
        if (character == '{') {
            ++modes_.back().brace_depth;
            emit(TokenKind::LBrace, position_, position_ + 1);
            ++position_;
            return true;
        }
        return false;
    }

    /// String, identifier or number. Returns true when a token was produced.
    bool scan_leaf(char character) {
        if (character == '"' || (character == 'r' && peek(1) == '"')) {
            start_string();
            return true;
        }
        if (is_ident_start(character)) {
            scan_identifier();
            return true;
        }
        if (is_digit(character)) {
            scan_number();
            return true;
        }
        return false;
    }

    enum class ModeKind : std::uint8_t { Normal, String, Interp };

    struct Mode {
        ModeKind kind = ModeKind::Normal;
        bool triple = false;
        bool raw = false;
        int brace_depth = 0;
    };

    [[nodiscard]] bool at_end() const { return position_ >= source_.size(); }

    [[nodiscard]] char peek(std::size_t offset = 0) const {
        const std::size_t index = position_ + offset;
        return index < source_.size() ? source_.at(index) : '\0';
    }

    [[nodiscard]] bool starts_with(std::string_view text) const {
        return source_.substr(position_, text.size()) == text;
    }

    void emit(TokenKind kind, std::size_t start, std::size_t end) {
        Token token;
        token.kind = kind;
        token.span = Span{
            .file = file_,
            .start = static_cast<std::uint32_t>(start),
            .end = static_cast<std::uint32_t>(end),
        };
        token.text = source_.substr(start, end - start);
        if (kind != TokenKind::Newline) {
            token.leading = std::move(pending_);
            pending_.clear();
            token.blank_line_before = blank_pending_;
            blank_pending_ = false;
            newline_run_ = 0;
        }
        tokens_.push_back(std::move(token));
    }

    void scan_newline() {
        const std::size_t start = position_;
        if (peek() == '\r' && peek(1) == '\n') {
            position_ += 2;
        } else {
            ++position_;
        }
        emit(TokenKind::Newline, start, position_);
        ++newline_run_;
        if (newline_run_ >= 2) {
            blank_pending_ = true;
        }
    }

    void scan_line_comment() {
        const std::size_t start = position_;
        const bool doc = peek(2) == '/';
        while (!at_end() && peek() != '\n' && peek() != '\r') {
            ++position_;
        }
        std::size_t end = position_;
        while (end > start && (source_.at(end - 1) == ' ' || source_.at(end - 1) == '\t')) {
            --end;
        }
        pending_.push_back(Trivia{
            .kind = doc ? TriviaKind::DocComment : TriviaKind::LineComment,
            .span =
                Span{
                    .file = file_,
                    .start = static_cast<std::uint32_t>(start),
                    .end = static_cast<std::uint32_t>(end),
                },
            .text = source_.substr(start, end - start),
        });
        newline_run_ = 0;
    }

    void scan_block_comment() {
        const std::size_t start = position_;
        position_ += 2;
        int depth = 1;
        while (!at_end() && depth > 0) {
            if (peek() == '/' && peek(1) == '*') {
                position_ += 2;
                ++depth;
            } else if (peek() == '*' && peek(1) == '/') {
                position_ += 2;
                --depth;
            } else {
                ++position_;
            }
        }
        if (depth > 0) {
            emit(TokenKind::Error, start, position_);
            return;
        }
        pending_.push_back(Trivia{
            .kind = TriviaKind::BlockComment,
            .span =
                Span{
                    .file = file_,
                    .start = static_cast<std::uint32_t>(start),
                    .end = static_cast<std::uint32_t>(position_),
                },
            .text = source_.substr(start, position_ - start),
        });
        newline_run_ = 0;
    }

    void start_string() {
        const std::size_t start = position_;
        const bool raw = peek() == 'r';
        if (raw) {
            ++position_;
        }
        const bool triple = peek() == '"' && peek(1) == '"' && peek(2) == '"';
        const std::size_t quote_length = triple ? 3 : 1;
        position_ += quote_length;
        emit(TokenKind::StringStart, start, position_);

        Mode mode;
        mode.kind = ModeKind::String;
        mode.triple = triple;
        mode.raw = raw;
        modes_.push_back(mode);
    }

    void scan_string_body() {
        const Mode mode = modes_.back();
        const std::size_t chunk_start = position_;

        while (!at_end()) {
            if (scan_string_edge(mode, chunk_start)) {
                return;
            }
            if (!mode.raw && peek() == '\\') {
                position_ += (position_ + 1 < source_.size()) ? 2 : 1;
                continue;
            }
            ++position_;
        }

        flush_string_part(chunk_start);
        emit(TokenKind::Error, position_, position_);
        modes_.pop_back();
    }

    /// Handle a closing delimiter, interpolation start or line end. Returns
    /// true when the string body is finished (or aborted).
    bool scan_string_edge(const Mode& mode, std::size_t chunk_start) {
        const char character = peek();
        if (mode.triple) {
            if (starts_with(R"(""")") && !is_escaped()) {
                flush_string_part(chunk_start);
                emit(TokenKind::StringEnd, position_, position_ + 3);
                position_ += 3;
                modes_.pop_back();
                return true;
            }
        } else {
            if (character == '"' && !is_escaped()) {
                flush_string_part(chunk_start);
                emit(TokenKind::StringEnd, position_, position_ + 1);
                ++position_;
                modes_.pop_back();
                return true;
            }
            if (character == '\n' || character == '\r') {
                flush_string_part(chunk_start);
                emit(TokenKind::Error, position_, position_ + 1);
                ++position_;
                modes_.pop_back();
                return true;
            }
        }

        if (!mode.raw && character == '{') {
            if (peek(1) == '{') {
                position_ += 2;
                return false;
            }
            flush_string_part(chunk_start);
            emit(TokenKind::InterpStart, position_, position_ + 1);
            ++position_;
            modes_.push_back(Mode{.kind = ModeKind::Interp});
            return true;
        }
        return false;
    }

    [[nodiscard]] bool is_escaped() const {
        std::size_t backslashes = 0;
        std::size_t index = position_;
        while (index > 0 && source_.at(index - 1) == '\\') {
            --index;
            ++backslashes;
        }
        return backslashes % 2 == 1;
    }

    void flush_string_part(std::size_t start) {
        if (position_ > start) {
            emit(TokenKind::StringPart, start, position_);
        }
    }

    void scan_identifier() {
        const std::size_t start = position_;
        while (!at_end() && is_ident_continue(peek())) {
            ++position_;
        }
        const std::string_view text = source_.substr(start, position_ - start);
        for (const Keyword& keyword : kKeywords) {
            if (keyword.spelling == text) {
                emit(keyword.kind, start, position_);
                return;
            }
        }
        emit(TokenKind::Identifier, start, position_);
    }

    void scan_digits() {
        while (!at_end() && (is_digit(peek()) || peek() == '_')) {
            ++position_;
        }
    }

    void scan_exponent() {
        if (peek() != 'e' && peek() != 'E') {
            return;
        }
        const std::size_t exponent = position_;
        ++position_;
        if (peek() == '+' || peek() == '-') {
            ++position_;
        }
        if (!is_digit(peek())) {
            position_ = exponent;
            return;
        }
        scan_digits();
    }

    [[nodiscard]] bool scan_fraction() {
        if (peek() != '.' || !is_digit(peek(1))) {
            return false;
        }
        ++position_;
        scan_digits();
        scan_exponent();
        return true;
    }

    [[nodiscard]] bool scan_duration_suffix() {
        const std::size_t suffix = position_;
        bool duration = false;
        if (peek() == 'm' && peek(1) == 's') {
            position_ += 2;
            duration = true;
        } else if (peek() == 's' || peek() == 'm' || peek() == 'h') {
            ++position_;
            duration = true;
        }
        if (duration && is_ident_continue(peek())) {
            position_ = suffix;
            return false;
        }
        return duration;
    }

    void scan_number() {
        const std::size_t start = position_;
        scan_digits();
        const bool is_float = scan_fraction();
        if (!is_float && scan_duration_suffix()) {
            emit(TokenKind::DurationLit, start, position_);
            return;
        }
        emit(is_float ? TokenKind::FloatLit : TokenKind::IntLit, start, position_);
    }

    void scan_punctuation() {
        const std::size_t start = position_;
        const char character = peek();
        const char next = peek(1);

        auto single = [this, start](TokenKind kind, std::size_t width = 1) {
            position_ += width;
            emit(kind, start, position_);
        };

        switch (character) {
        case '(':
            single(TokenKind::LParen);
            return;
        case ')':
            single(TokenKind::RParen);
            return;
        case '{':
            single(TokenKind::LBrace);
            return;
        case '}':
            single(TokenKind::RBrace);
            return;
        case '[':
            single(TokenKind::LBracket);
            return;
        case ']':
            single(TokenKind::RBracket);
            return;
        case ',':
            single(TokenKind::Comma);
            return;
        case ';':
            single(TokenKind::Semicolon);
            return;
        case ':':
            single(TokenKind::Colon);
            return;
        case '?':
            single(TokenKind::Question);
            return;
        case '+':
            single(TokenKind::Plus);
            return;
        case '-':
            if (next == '>') {
                single(TokenKind::Arrow, 2);
            } else {
                single(TokenKind::Minus);
            }
            return;
        case '*':
            single(TokenKind::Star);
            return;
        case '/':
            single(TokenKind::Slash);
            return;
        case '%':
            single(TokenKind::Percent);
            return;
        case '!':
            if (next == '=') {
                single(TokenKind::NotEq, 2);
            } else {
                single(TokenKind::Bang);
            }
            return;
        case '|':
            single(TokenKind::Pipe);
            return;
        case '.':
            if (next == '.') {
                single(TokenKind::DotDot, 2);
            } else {
                single(TokenKind::Dot);
            }
            return;
        case '=':
            if (next == '=') {
                single(TokenKind::EqEq, 2);
            } else if (next == '>') {
                single(TokenKind::FatArrow, 2);
            } else {
                single(TokenKind::Eq);
            }
            return;
        case '<':
            single(next == '=' ? TokenKind::Le : TokenKind::Lt, next == '=' ? 2 : 1);
            return;
        case '>':
            single(next == '=' ? TokenKind::Ge : TokenKind::Gt, next == '=' ? 2 : 1);
            return;
        default:
            position_ += 1;
            emit(TokenKind::Error, start, position_);
            return;
        }
    }

    std::string_view source_;
    FileId file_;
    std::size_t position_ = 0;
    std::vector<Mode> modes_;
    std::vector<Token> tokens_;
    std::vector<Trivia> pending_;
    bool blank_pending_ = false;
    int newline_run_ = 0;
};

} // namespace

std::string_view token_kind_name(TokenKind kind) {
    const auto index = static_cast<std::size_t>(kind);
    if (index < kTokenNames.size()) {
        return kTokenNames.at(index);
    }
    return "<invalid>";
}

bool is_keyword(TokenKind kind) {
    return kind >= TokenKind::KwModule && kind <= TokenKind::KwAllow;
}

bool is_comment(TriviaKind kind) {
    switch (kind) {
    case TriviaKind::LineComment:
    case TriviaKind::BlockComment:
    case TriviaKind::DocComment:
        return true;
    }
    return false;
}

std::vector<Token> lex(std::string_view src, FileId file) { return Lexer(src, file).run(); }

} // namespace leash::syntax
