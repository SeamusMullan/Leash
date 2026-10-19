// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "leash/syntax/lexer.hpp"
#include "leash/syntax/token.hpp"

namespace {

using leash::syntax::FileId;
using leash::syntax::Token;
using leash::syntax::TokenKind;

[[nodiscard]] std::vector<Token> lex_text(std::string_view text) {
    return leash::syntax::lex(text, FileId{1});
}

[[nodiscard]] std::vector<TokenKind> kinds(const std::vector<Token>& tokens) {
    std::vector<TokenKind> result;
    result.reserve(tokens.size());
    for (const Token& token : tokens) {
        result.push_back(token.kind);
    }
    return result;
}

} // namespace

TEST_CASE("lexer produces keyword and punctuation tokens", "[syntax][lexer]") {
    const std::string source = "module a.b let x = 1 + 2";
    const std::vector<Token> tokens = lex_text(source);
    const std::vector<TokenKind> expected = {
        TokenKind::KwModule, TokenKind::Identifier, TokenKind::Dot, TokenKind::Identifier,
        TokenKind::KwLet,    TokenKind::Identifier, TokenKind::Eq,  TokenKind::IntLit,
        TokenKind::Plus,     TokenKind::IntLit,     TokenKind::Eof,
    };
    REQUIRE(kinds(tokens) == expected);
    CHECK(tokens.at(0).text == "module");
    CHECK(tokens.at(3).text == "b");
}

TEST_CASE("lexer distinguishes identifiers from hard keywords", "[syntax][lexer]") {
    const std::string source = "input output ensure check by expect within on_timeout severity";
    const std::vector<Token> tokens = lex_text(source);
    for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
        CHECK(tokens.at(index).kind == TokenKind::Identifier);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("lexer records byte spans that slice the source", "[syntax][lexer]") {
    const std::string source = "let answer = 42\n";
    const std::vector<Token> tokens = lex_text(source);

    for (const Token& token : tokens) {
        if (token.kind == TokenKind::Eof) {
            CHECK(token.span.start == source.size());
            CHECK(token.span.end == source.size());
            continue;
        }
        CHECK(token.span.end > token.span.start);
        CHECK(token.span.end <= source.size());
        CHECK(token.text == std::string_view{source}.substr(token.span.start, token.span.size()));
    }

    CHECK(tokens.front().span.start == 0);
    CHECK(tokens.front().span.end == 3);
    CHECK(tokens.back().span.file == FileId{1});
}

TEST_CASE("lexer handles numbers, floats and durations", "[syntax][lexer]") {
    const std::string source = "1 1_000 1.5 1.5e3 500ms 30s 5m 2h 2hx";
    const std::vector<Token> tokens = lex_text(source);
    CHECK(tokens.at(0).kind == TokenKind::IntLit);
    CHECK(tokens.at(1).kind == TokenKind::IntLit);
    CHECK(tokens.at(1).text == "1_000");
    CHECK(tokens.at(2).kind == TokenKind::FloatLit);
    CHECK(tokens.at(3).kind == TokenKind::FloatLit);
    CHECK(tokens.at(4).kind == TokenKind::DurationLit);
    CHECK(tokens.at(5).kind == TokenKind::DurationLit);
    CHECK(tokens.at(6).kind == TokenKind::DurationLit);
    CHECK(tokens.at(7).kind == TokenKind::DurationLit);
    CHECK(tokens.at(8).kind == TokenKind::IntLit);
    CHECK(tokens.at(9).kind == TokenKind::Identifier);
    CHECK(tokens.at(9).text == "hx");
}

TEST_CASE("lexer attaches comments as trivia to the following token", "[syntax][lexer]") {
    const std::string source = "// line\n/* outer /* nested */ done */\n/// doc\nlet x = 1\n";
    const std::vector<Token> tokens = lex_text(source);
    const Token& let = tokens.at(3);
    REQUIRE(let.kind == TokenKind::KwLet);
    REQUIRE(let.leading.size() == 3);
    CHECK(let.leading.at(0).kind == leash::syntax::TriviaKind::LineComment);
    CHECK(let.leading.at(0).text == "// line");
    CHECK(let.leading.at(1).kind == leash::syntax::TriviaKind::BlockComment);
    CHECK(let.leading.at(1).text == "/* outer /* nested */ done */");
    CHECK(let.leading.at(2).kind == leash::syntax::TriviaKind::DocComment);
    CHECK(let.leading.at(2).text == "/// doc");
}

TEST_CASE("lexer marks a blank line before the next token", "[syntax][lexer]") {
    const std::string source = "let a = 1\n\nlet b = 2\n";
    const std::vector<Token> tokens = lex_text(source);
    REQUIRE(tokens.size() > 6);
    CHECK_FALSE(tokens.at(0).blank_line_before);
    CHECK(tokens.at(6).kind == TokenKind::KwLet);
    CHECK(tokens.at(6).blank_line_before);
}

TEST_CASE("lexer tokenises a plain string", "[syntax][lexer]") {
    const std::vector<Token> tokens = lex_text("\"hello\"");
    const std::vector<TokenKind> expected = {
        TokenKind::StringStart,
        TokenKind::StringPart,
        TokenKind::StringEnd,
        TokenKind::Eof,
    };
    REQUIRE(kinds(tokens) == expected);
    CHECK(tokens.at(0).text == "\"");
    CHECK(tokens.at(1).text == "hello");
    CHECK(tokens.at(2).text == "\"");
}

TEST_CASE("lexer tokenises interpolation and brace escapes", "[syntax][lexer]") {
    const std::string source = "\"hi {name} {{literal}}\"";
    const std::vector<Token> tokens = lex_text(source);
    const std::vector<TokenKind> expected = {
        TokenKind::StringStart, TokenKind::StringPart, TokenKind::InterpStart,
        TokenKind::Identifier,  TokenKind::InterpEnd,  TokenKind::StringPart,
        TokenKind::StringEnd,   TokenKind::Eof,
    };
    REQUIRE(kinds(tokens) == expected);
    CHECK(tokens.at(1).text == "hi ");
    CHECK(tokens.at(3).text == "name");
    CHECK(tokens.at(5).text == " {{literal}}");
}

TEST_CASE("lexer supports nested strings inside interpolation", "[syntax][lexer]") {
    const std::string source = R"("{f("x")}")";
    const std::vector<TokenKind> token_kinds = kinds(lex_text(source));
    const std::vector<TokenKind> expected = {
        TokenKind::StringStart, TokenKind::InterpStart, TokenKind::Identifier, TokenKind::LParen,
        TokenKind::StringStart, TokenKind::StringPart,  TokenKind::StringEnd,  TokenKind::RParen,
        TokenKind::InterpEnd,   TokenKind::StringEnd,   TokenKind::Eof,
    };
    REQUIRE(token_kinds == expected);
}

TEST_CASE("lexer keeps raw and triple-quoted strings verbatim", "[syntax][lexer]") {
    const std::string raw = R"(r"\n{x}")";
    const std::vector<Token> raw_tokens = lex_text(raw);
    REQUIRE(raw_tokens.size() == 4);
    CHECK(raw_tokens.at(0).text == "r\"");
    CHECK(raw_tokens.at(1).text == "\\n{x}");
    CHECK(raw_tokens.at(2).text == "\"");

    const std::string triple = R"("""
hello
  world
""")";
    const std::vector<Token> triple_tokens = lex_text(triple);
    REQUIRE(triple_tokens.size() == 4);
    CHECK(triple_tokens.at(0).text == "\"\"\"");
    CHECK(triple_tokens.at(1).text == "\nhello\n  world\n");
    CHECK(triple_tokens.at(2).text == "\"\"\"");
}

TEST_CASE("lexer survives a megabyte of arbitrary bytes", "[syntax][lexer]") {
    std::string bytes(static_cast<std::size_t>(1024) * 1024, '\0');
    std::uint32_t state = 0x12345678U;
    for (char& byte : bytes) {
        state = (state * 1664525U) + 1013904223U;
        byte = static_cast<char>(state >> 24U);
    }
    const auto started = std::chrono::steady_clock::now();
    const std::vector<Token> tokens = lex_text(bytes);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    REQUIRE_FALSE(tokens.empty());
    CHECK(tokens.back().kind == TokenKind::Eof);
    CHECK(tokens.back().span.start == bytes.size());
    CHECK(tokens.back().span.end == bytes.size());
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 1000);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): Catch2 macros inflate it
TEST_CASE("lexer reports unterminated strings and invalid bytes as Error", "[syntax][lexer]") {
    const std::vector<Token> string_tokens = lex_text("\"abc");
    CHECK(string_tokens.at(0).kind == TokenKind::StringStart);
    CHECK(string_tokens.at(1).kind == TokenKind::StringPart);
    CHECK(string_tokens.at(2).kind == TokenKind::Error);

    const std::vector<Token> byte_tokens = lex_text("let @ = 1");
    bool saw_error = false;
    for (const Token& token : byte_tokens) {
        if (token.kind == TokenKind::Error) {
            saw_error = true;
            CHECK(token.text == "@");
        }
    }
    CHECK(saw_error);
}
