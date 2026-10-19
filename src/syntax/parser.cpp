// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/syntax/parser.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "leash/syntax/ast.hpp"
#include "leash/syntax/diagnostic.hpp"
#include "leash/syntax/lexer.hpp"
#include "leash/syntax/token.hpp"

namespace leash::syntax {
namespace {

constexpr int kMaxDepth = 256;
constexpr int kOrPrecedence = 1;
constexpr int kAndPrecedence = 2;
constexpr int kNotPrecedence = 3;
constexpr int kComparePrecedence = 4;
constexpr int kAddPrecedence = 5;
constexpr int kMulPrecedence = 6;
constexpr int kUnaryPrecedence = 7;

[[nodiscard]] ast::Comment::Kind comment_kind(TriviaKind kind) {
    switch (kind) {
    case TriviaKind::LineComment:
        return ast::Comment::Kind::Line;
    case TriviaKind::BlockComment:
        return ast::Comment::Kind::Block;
    case TriviaKind::DocComment:
        return ast::Comment::Kind::Doc;
    }
    return ast::Comment::Kind::Line;
}

[[nodiscard]] std::string quote_token(const Token& token) {
    if (token.kind == TokenKind::Eof) {
        return "end of file";
    }
    if (token.kind == TokenKind::Newline) {
        return "newline";
    }
    if (token.kind == TokenKind::Error) {
        return "invalid token";
    }
    std::string text{token.text};
    if (text.size() > 24) {
        text.resize(24);
        text += "...";
    }
    return "'" + text + "'";
}

[[nodiscard]] std::optional<std::pair<ast::BinaryOp, int>> binary_operator(TokenKind kind) {
    if (kind == TokenKind::KwOr) {
        return std::pair{ast::BinaryOp::Or, kOrPrecedence};
    }
    if (kind == TokenKind::KwAnd) {
        return std::pair{ast::BinaryOp::And, kAndPrecedence};
    }
    if (kind == TokenKind::EqEq) {
        return std::pair{ast::BinaryOp::Eq, kComparePrecedence};
    }
    if (kind == TokenKind::NotEq) {
        return std::pair{ast::BinaryOp::Ne, kComparePrecedence};
    }
    if (kind == TokenKind::Lt) {
        return std::pair{ast::BinaryOp::Lt, kComparePrecedence};
    }
    if (kind == TokenKind::Le) {
        return std::pair{ast::BinaryOp::Le, kComparePrecedence};
    }
    if (kind == TokenKind::Gt) {
        return std::pair{ast::BinaryOp::Gt, kComparePrecedence};
    }
    if (kind == TokenKind::Ge) {
        return std::pair{ast::BinaryOp::Ge, kComparePrecedence};
    }
    if (kind == TokenKind::Plus) {
        return std::pair{ast::BinaryOp::Add, kAddPrecedence};
    }
    if (kind == TokenKind::Minus) {
        return std::pair{ast::BinaryOp::Sub, kAddPrecedence};
    }
    if (kind == TokenKind::Star) {
        return std::pair{ast::BinaryOp::Mul, kMulPrecedence};
    }
    if (kind == TokenKind::Slash) {
        return std::pair{ast::BinaryOp::Div, kMulPrecedence};
    }
    if (kind == TokenKind::Percent) {
        return std::pair{ast::BinaryOp::Rem, kMulPrecedence};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view> unsupported_expression_keyword(TokenKind kind) {
    if (kind == TokenKind::KwMatch) {
        return "match";
    }
    if (kind == TokenKind::KwFor) {
        return "for";
    }
    if (kind == TokenKind::KwWhile) {
        return "while";
    }
    if (kind == TokenKind::KwLoop) {
        return "loop";
    }
    if (kind == TokenKind::KwReturn) {
        return "return";
    }
    if (kind == TokenKind::KwBreak) {
        return "break";
    }
    if (kind == TokenKind::KwContinue) {
        return "continue";
    }
    if (kind == TokenKind::KwRun) {
        return "run";
    }
    if (kind == TokenKind::KwSpawn) {
        return "spawn";
    }
    if (kind == TokenKind::KwAwait) {
        return "await";
    }
    if (kind == TokenKind::KwParallel) {
        return "parallel";
    }
    if (kind == TokenKind::KwRace) {
        return "race";
    }
    if (kind == TokenKind::KwRetry) {
        return "retry";
    }
    if (kind == TokenKind::KwAttempt) {
        return "attempt";
    }
    if (kind == TokenKind::KwRecover) {
        return "recover";
    }
    if (kind == TokenKind::KwVerify) {
        return "verify";
    }
    if (kind == TokenKind::KwCheckpoint) {
        return "checkpoint";
    }
    if (kind == TokenKind::KwBudget) {
        return "budget expression";
    }
    if (kind == TokenKind::KwAllow) {
        return "allow expression";
    }
    return std::nullopt;
}

class Parser {
  public:
    Parser(std::string_view source, FileId file)
        : source_(source), file_(file), tokens_(lex(source, file)) {
        if (tokens_.empty()) {
            tokens_.push_back(Token{
                .kind = TokenKind::Eof,
                .span = Span{.file = file, .start = 0, .end = 0},
                .text = {},
                .leading = {},
                .blank_line_before = false,
            });
        }
    }

    Parsed run() {
        ast::Program program;
        program.id = next_id();
        const std::uint32_t start = current().span.start;

        skip_newlines();
        if (at(TokenKind::KwModule)) {
            program.leading = take_leading(current());
            advance();
            program.module_path = parse_path();
            skip_newlines();
        }

        while (!at_eof()) {
            skip_separators();
            if (at_eof()) {
                break;
            }
            ast::Leading leading = take_leading(current());
            const std::size_t before = position_;
            std::optional<ast::Item> item = parse_item(std::move(leading));
            if (item.has_value()) {
                program.items.push_back(std::move(*item));
            }
            if (position_ == before) {
                recover_to_item();
                if (position_ == before) {
                    advance();
                }
            }
        }

        program.trailing = take_leading(current());
        program.span =
            Span{.file = file_, .start = start, .end = std::max(last_end_, current().span.end)};
        return Parsed{.program = std::move(program), .diagnostics = std::move(diagnostics_)};
    }

  private:
    // ------------------------------------------------------------- token access

    [[nodiscard]] const Token& peek(std::size_t offset = 0) const {
        const std::size_t index = std::min(position_ + offset, tokens_.size() - 1);
        return tokens_.at(index);
    }

    [[nodiscard]] TokenKind kind_at(std::size_t index) const {
        return tokens_.at(std::min(index, tokens_.size() - 1)).kind;
    }

    [[nodiscard]] const Token& current() const { return tokens_.at(position_); }

    [[nodiscard]] bool at(TokenKind kind) const { return current().kind == kind; }

    [[nodiscard]] bool at_eof() const { return current().kind == TokenKind::Eof; }

    [[nodiscard]] bool at_ident(std::string_view text) const {
        return current().kind == TokenKind::Identifier && current().text == text;
    }

    const Token& advance() {
        const Token& token = tokens_.at(position_);
        last_end_ = token.span.end;
        if (position_ + 1 < tokens_.size()) {
            ++position_;
        }
        return token;
    }

    bool match(TokenKind kind) {
        if (!at(kind)) {
            return false;
        }
        advance();
        return true;
    }

    Token expect(TokenKind kind, std::string_view what) {
        if (at(kind)) {
            return advance();
        }
        report("E0101", "expected " + std::string{what}, current().span,
               "expected " + std::string{what} + " here");
        return current();
    }

    [[nodiscard]] Span span_from(std::uint32_t start) const {
        return Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
    }

    [[nodiscard]] NodeId next_id() { return NodeId{next_node_++}; }

    void skip_newlines() {
        while (at(TokenKind::Newline)) {
            advance();
        }
    }

    void skip_separators() {
        while (at(TokenKind::Newline) || at(TokenKind::Semicolon)) {
            advance();
        }
    }

    [[nodiscard]] static ast::Leading take_leading(const Token& token) {
        ast::Leading leading;
        leading.blank_before = token.blank_line_before;
        leading.comments.reserve(token.leading.size());
        for (const Trivia& trivia : token.leading) {
            leading.comments.push_back(ast::Comment{
                .kind = comment_kind(trivia.kind),
                .span = trivia.span,
                .text = trivia.text,
            });
        }
        return leading;
    }

    // ------------------------------------------------------------- diagnostics

    void report(std::string_view code, std::string message, Span span, std::string label,
                std::optional<std::string> help = std::nullopt) {
        Diagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.severity = Severity::Error;
        diagnostic.message = std::move(message);
        diagnostic.labels.emplace_back(span, std::move(label));
        diagnostic.help = std::move(help);
        diagnostics_.push_back(std::move(diagnostic));
    }

    void report_unsupported(const Token& token) {
        std::string name{token.text};
        report("E0100", "'" + name + "' is not supported yet (WP-07)", token.span,
               "'" + name + "' arrives in WP-07",
               "remove this construct or wait for the full grammar");
    }

    // --------------------------------------------------------------- recovery

    [[nodiscard]] bool is_item_start() const {
        const TokenKind kind = current().kind;
        if (kind == TokenKind::KwUse || kind == TokenKind::KwLet || kind == TokenKind::KwFn ||
            kind == TokenKind::KwType || kind == TokenKind::KwAgent || kind == TokenKind::KwTask ||
            kind == TokenKind::KwFlow || kind == TokenKind::KwPub || kind == TokenKind::KwEnum ||
            kind == TokenKind::KwTool || kind == TokenKind::KwMemory) {
            return true;
        }
        return at_ident("test") && peek(1).kind == TokenKind::StringStart;
    }

    /// Skip a construct that is recognised but unsupported: its balanced
    /// parentheses, brackets and braces, stopping at the end of the line.
    void skip_construct() {
        int depth = 0;
        while (!at_eof()) {
            const TokenKind kind = current().kind;
            if (depth == 0 && (kind == TokenKind::Newline || kind == TokenKind::Semicolon)) {
                return;
            }
            if (kind == TokenKind::LParen || kind == TokenKind::LBrace ||
                kind == TokenKind::LBracket) {
                ++depth;
            } else if (kind == TokenKind::RParen || kind == TokenKind::RBrace ||
                       kind == TokenKind::RBracket) {
                if (depth == 0) {
                    return;
                }
                --depth;
            }
            advance();
            if (depth == 0 && at(TokenKind::Newline)) {
                return;
            }
        }
    }

    void recover_to_item() {
        int depth = 0;
        while (!at_eof()) {
            const TokenKind previous = position_ == 0 ? TokenKind::Newline : peek(-1).kind;
            if (depth == 0 && is_item_start() &&
                (previous == TokenKind::Newline || previous == TokenKind::Semicolon ||
                 previous == TokenKind::RBrace || position_ == 0)) {
                return;
            }
            const TokenKind kind = current().kind;
            if (kind == TokenKind::LParen || kind == TokenKind::LBrace ||
                kind == TokenKind::LBracket) {
                ++depth;
            } else if ((kind == TokenKind::RParen || kind == TokenKind::RBrace ||
                        kind == TokenKind::RBracket) &&
                       depth > 0) {
                --depth;
            }
            advance();
        }
    }

    void recover_to_stmt() {
        int depth = 0;
        while (!at_eof()) {
            const TokenKind kind = current().kind;
            if (depth == 0 && (kind == TokenKind::Newline || kind == TokenKind::Semicolon ||
                               kind == TokenKind::RBrace)) {
                return;
            }
            if (kind == TokenKind::LParen || kind == TokenKind::LBrace ||
                kind == TokenKind::LBracket) {
                ++depth;
            } else if ((kind == TokenKind::RParen || kind == TokenKind::RBrace ||
                        kind == TokenKind::RBracket) &&
                       depth > 0) {
                --depth;
            }
            advance();
        }
    }

    void report_depth() {
        if (!depth_reported_) {
            report("E0107", "nesting is too deep", current().span, "parser depth limit reached",
                   "simplify the expression or split it into smaller definitions");
            depth_reported_ = true;
        }
    }

    // ------------------------------------------------------------------- items

    std::optional<ast::Item> parse_item(ast::Leading leading) {
        bool is_pub = false;
        if (at(TokenKind::KwPub)) {
            is_pub = true;
            advance();
        }
        const std::uint32_t start = current().span.start;

        if (at(TokenKind::KwUse)) {
            return parse_use(std::move(leading), start);
        }
        if (at(TokenKind::KwLet)) {
            return parse_const(std::move(leading), is_pub, start);
        }
        if (at(TokenKind::KwType)) {
            return parse_type_decl(std::move(leading), is_pub, start);
        }
        if (at(TokenKind::KwFn)) {
            return parse_fn(std::move(leading), is_pub, start);
        }
        if (at(TokenKind::KwAgent)) {
            return parse_agent(std::move(leading), is_pub, start);
        }
        if (at(TokenKind::KwTask)) {
            return parse_task(std::move(leading), is_pub, start);
        }
        if (at(TokenKind::KwFlow)) {
            return parse_flow(std::move(leading), is_pub, start);
        }
        if (at(TokenKind::KwEnum) || at(TokenKind::KwTool) || at(TokenKind::KwMemory) ||
            at(TokenKind::KwVar)) {
            report_unsupported(current());
            skip_construct();
            return std::nullopt;
        }
        if (at_ident("test") && peek(1).kind == TokenKind::StringStart) {
            report("E0100", "'test' is not supported yet (WP-07)", current().span,
                   "'test' declarations arrive in WP-07", std::nullopt);
            skip_construct();
            return std::nullopt;
        }

        report("E0110", "expected an item, found " + quote_token(current()), current().span,
               "expected one of `use`, `let`, `type`, `fn`, `agent`, `task`, `flow`", std::nullopt);
        recover_to_item();
        return std::nullopt;
    }

    std::optional<ast::Item> parse_use(ast::Leading leading, std::uint32_t start) {
        const NodeId node = next_id();
        advance();
        ast::UseDecl decl;
        decl.id = node;
        decl.leading = std::move(leading);
        decl.path = parse_path();

        if (at(TokenKind::Dot) && peek(1).kind == TokenKind::LBrace) {
            advance();
            advance();
            decl.grouped = true;
            skip_newlines();
            while (!at(TokenKind::RBrace) && !at_eof()) {
                decl.names.push_back(parse_ident("an imported name"));
                skip_newlines();
                if (!match(TokenKind::Comma)) {
                    break;
                }
                skip_newlines();
            }
            expect(TokenKind::RBrace, "'}' to close the import group");
        }

        decl.span = span_from(start);
        return ast::Item{std::move(decl)};
    }

    std::optional<ast::Item> parse_const(ast::Leading leading, bool is_pub, std::uint32_t start) {
        const NodeId node = next_id();
        advance();
        ast::ConstDecl decl;
        decl.id = node;
        decl.leading = std::move(leading);
        decl.is_pub = is_pub;
        decl.name = parse_ident("a constant name");
        if (match(TokenKind::Colon)) {
            decl.type = parse_type();
        }
        expect(TokenKind::Eq, "'=' in a `let` declaration");
        decl.value = parse_expr();
        decl.span = span_from(start);
        return ast::Item{std::move(decl)};
    }

    std::optional<ast::Item> parse_type_decl(ast::Leading leading, bool is_pub,
                                             std::uint32_t start) {
        const NodeId node = next_id();
        advance();
        ast::TypeDecl decl;
        decl.id = node;
        decl.leading = std::move(leading);
        decl.is_pub = is_pub;
        decl.name = parse_ident("a type name");
        if (at(TokenKind::Lt)) {
            report("E0100", "'generics' are not supported yet (WP-07)", current().span,
                   "generic parameters arrive in WP-07", std::nullopt);
            skip_construct();
            return std::nullopt;
        }
        expect(TokenKind::Eq, "'=' in a type declaration");
        decl.type = parse_type();
        decl.span = span_from(start);
        return ast::Item{std::move(decl)};
    }

    std::optional<ast::Item> parse_fn(ast::Leading leading, bool is_pub, std::uint32_t start) {
        const NodeId node = next_id();
        advance();
        ast::FnDecl decl;
        decl.id = node;
        decl.leading = std::move(leading);
        decl.is_pub = is_pub;
        decl.name = parse_ident("a function name");
        if (at(TokenKind::Lt)) {
            report("E0100", "'generics' are not supported yet (WP-07)", current().span,
                   "generic parameters arrive in WP-07", std::nullopt);
            skip_construct();
            return std::nullopt;
        }
        decl.params = parse_params();
        if (match(TokenKind::Arrow)) {
            decl.ret = parse_type();
        }
        if (at(TokenKind::Bang)) {
            decl.effects = parse_effect_clause();
        }
        decl.body = parse_block();
        decl.span = span_from(start);
        return ast::Item{std::move(decl)};
    }

    std::vector<ast::Param> parse_params() {
        std::vector<ast::Param> params;
        expect(TokenKind::LParen, "'(' to start the parameter list");
        skip_newlines();
        while (!at(TokenKind::RParen) && !at_eof()) {
            ast::Param param;
            param.leading = take_leading(current());
            param.name = parse_ident("a parameter name");
            if (match(TokenKind::Colon)) {
                param.type = parse_type();
            }
            if (match(TokenKind::Eq)) {
                param.default_value = parse_expr();
            }
            params.push_back(std::move(param));
            skip_newlines();
            if (!match(TokenKind::Comma)) {
                break;
            }
            skip_newlines();
        }
        expect(TokenKind::RParen, "')' to close the parameter list");
        return params;
    }

    std::optional<ast::EffectClause> parse_effect_clause() {
        ast::EffectClause clause;
        const std::uint32_t start = current().span.start;
        advance(); // '!'
        expect(TokenKind::LBrace, "'{' to start the effect clause");
        skip_newlines();
        while (!at(TokenKind::RBrace) && !at_eof()) {
            clause.effects.push_back(parse_path());
            skip_newlines();
            if (!match(TokenKind::Comma)) {
                break;
            }
            skip_newlines();
        }
        expect(TokenKind::RBrace, "'}' to close the effect clause");
        clause.span = span_from(start);
        return clause;
    }

    std::optional<ast::Item> parse_agent(ast::Leading leading, bool is_pub, std::uint32_t start) {
        const NodeId node = next_id();
        advance();
        ast::AgentDecl decl;
        decl.id = node;
        decl.leading = std::move(leading);
        decl.is_pub = is_pub;
        decl.name = parse_ident("an agent name");
        expect(TokenKind::Eq, "'=' in an agent declaration");
        decl.value = parse_expr();
        decl.span = span_from(start);
        return ast::Item{std::move(decl)};
    }

    std::optional<ast::Item> parse_task(ast::Leading leading, bool is_pub, std::uint32_t start) {
        const NodeId node = next_id();
        advance();
        ast::TaskDecl decl;
        decl.id = node;
        decl.leading = std::move(leading);
        decl.is_pub = is_pub;
        decl.name = parse_ident("a task name");
        expect(TokenKind::LBrace, "'{' to start the task body");
        skip_newlines();

        while (!at(TokenKind::RBrace) && !at_eof()) {
            if (at_ident("input")) {
                ast::TaskInput section;
                section.id = next_id();
                section.leading = take_leading(current());
                const std::uint32_t section_start = current().span.start;
                advance();
                section.type = parse_record_type();
                section.span = span_from(section_start);
                decl.sections.emplace_back(std::move(section));
            } else if (at_ident("output")) {
                ast::TaskOutput section;
                section.id = next_id();
                section.leading = take_leading(current());
                const std::uint32_t section_start = current().span.start;
                advance();
                section.type = parse_type();
                section.span = span_from(section_start);
                decl.sections.emplace_back(std::move(section));
            } else if (at(TokenKind::KwBudget)) {
                ast::TaskBudget section;
                section.id = next_id();
                section.leading = take_leading(current());
                const std::uint32_t section_start = current().span.start;
                advance();
                if (at(TokenKind::LBrace)) {
                    section.value = parse_record_lit();
                } else if (at(TokenKind::LParen)) {
                    advance();
                    section.value = parse_expr();
                    expect(TokenKind::RParen, "')' after the budget expression");
                } else {
                    report("E0101",
                           "expected a record literal or parenthesised expression after `budget`",
                           current().span, "expected `{ ... }` here", std::nullopt);
                    section.value = error_expr(current().span);
                }
                section.span = span_from(section_start);
                decl.sections.emplace_back(std::move(section));
            } else if (at(TokenKind::KwAllow) || at_ident("require") || at_ident("ensure") ||
                       at_ident("deny")) {
                report_unsupported(current());
                skip_construct();
            } else {
                report("E0102", "unexpected " + quote_token(current()) + " in a task body",
                       current().span, "expected `input`, `output` or `budget`", std::nullopt);
                skip_construct();
                if (!at(TokenKind::Newline) && !at(TokenKind::RBrace) && !at_eof()) {
                    advance();
                }
            }
            skip_separators();
        }

        expect(TokenKind::RBrace, "'}' to close the task body");
        decl.span = span_from(start);
        return ast::Item{std::move(decl)};
    }

    std::optional<ast::Item> parse_flow(ast::Leading leading, bool is_pub, std::uint32_t start) {
        const NodeId node = next_id();
        advance();
        ast::FlowDecl decl;
        decl.id = node;
        decl.leading = std::move(leading);
        decl.is_pub = is_pub;
        decl.name = parse_ident("a flow name");
        expect(TokenKind::LParen, "'(' after the flow name");
        decl.param.leading = take_leading(current());
        decl.param.name = parse_ident("a flow parameter");
        if (match(TokenKind::Colon)) {
            decl.param.type = parse_type();
        }
        expect(TokenKind::RParen, "')' to close the flow parameter");
        if (match(TokenKind::Arrow)) {
            decl.ret = parse_type();
        }
        if (at(TokenKind::Bang)) {
            decl.effects = parse_effect_clause();
        }
        decl.body = parse_block();
        decl.span = span_from(start);
        return ast::Item{std::move(decl)};
    }

    // ------------------------------------------------------------------ blocks

    ast::Block parse_block() {
        const NodeId node = next_id();
        const std::uint32_t start = expect(TokenKind::LBrace, "'{' to start a block").span.start;
        ast::Block block = parse_block_body(node, start);
        block.span = span_from(start);
        expect(TokenKind::RBrace, "'}' to close the block");
        return block;
    }

    /// Parse statements after a `{` (or after `=>` for a trailing lambda) up to
    /// the closing `}`. The caller owns the closing brace.
    ast::Block parse_block_body(NodeId node, std::uint32_t start) {
        ast::Block block;
        block.id = node;
        skip_separators();

        while (!at(TokenKind::RBrace) && !at_eof()) {
            ast::Leading leading = take_leading(current());
            const std::size_t before = position_;
            std::optional<ast::Stmt> stmt = parse_statement(std::move(leading));
            if (stmt.has_value()) {
                block.stmts.push_back(std::move(*stmt));
            }
            if (position_ == before) {
                recover_to_stmt();
                if (position_ == before) {
                    advance();
                }
            }
            if (position_ == before) {
                break;
            }

            if (at(TokenKind::Newline) || at(TokenKind::Semicolon)) {
                skip_separators();
            } else if (!at(TokenKind::RBrace) && !at_eof()) {
                report("E0106", "expected a newline or ';' after this statement", current().span,
                       "add a newline here", std::nullopt);
                recover_to_stmt();
                skip_separators();
            }
        }

        block.trailing = take_leading(current());
        block.span = span_from(start);
        return block;
    }

    std::optional<ast::Stmt> parse_statement(ast::Leading leading) {
        if (at(TokenKind::KwLet)) {
            ast::LetStmt stmt;
            stmt.id = next_id();
            const std::uint32_t start = current().span.start;
            stmt.leading = std::move(leading);
            advance();
            if (at(TokenKind::LParen)) {
                report("E0100", "'tuple patterns' are not supported yet (WP-07)", current().span,
                       "destructuring patterns arrive in WP-07", std::nullopt);
                skip_construct();
                return std::nullopt;
            }
            stmt.name = parse_ident("a binding name");
            if (match(TokenKind::Colon)) {
                stmt.type = parse_type();
            }
            expect(TokenKind::Eq, "'=' in a `let` statement");
            stmt.value = parse_expr();
            stmt.span = span_from(start);
            if (at(TokenKind::Eq)) {
                report("E0100", "'assignment' is not supported yet (WP-07)", current().span,
                       "assignments arrive in WP-07", std::nullopt);
                recover_to_stmt();
            }
            return ast::Stmt{std::move(stmt)};
        }

        if (at(TokenKind::KwVar)) {
            report_unsupported(current());
            skip_construct();
            return std::nullopt;
        }
        if (at(TokenKind::KwCheckpoint)) {
            report_unsupported(current());
            skip_construct();
            return std::nullopt;
        }
        if (at_ident("state") && peek(1).kind == TokenKind::LBrace) {
            report_unsupported(current());
            skip_construct();
            return std::nullopt;
        }
        if (at_ident("state") && peek(1).kind == TokenKind::Dot) {
            report("E0100", "'assignment' is not supported yet (WP-07)", current().span,
                   "mutable state arrives in WP-07", std::nullopt);
            recover_to_stmt();
            return std::nullopt;
        }

        ast::ExprStmt stmt;
        stmt.id = next_id();
        const std::uint32_t start = current().span.start;
        stmt.leading = std::move(leading);
        stmt.expr = parse_expr();
        stmt.span = span_from(start);

        if (at(TokenKind::Eq)) {
            report("E0100", "'assignment' is not supported yet (WP-07)", current().span,
                   "assignments arrive in WP-07", std::nullopt);
            recover_to_stmt();
        } else if (at(TokenKind::Plus) && peek(1).kind == TokenKind::Eq) {
            report("E0100", "'compound assignment' is not supported yet (WP-07)", current().span,
                   "assignments arrive in WP-07", std::nullopt);
            recover_to_stmt();
        }
        return ast::Stmt{std::move(stmt)};
    }

    // ------------------------------------------------------------- expressions

    ast::ExprPtr parse_expr(int min_precedence = 0) {
        if (depth_ >= kMaxDepth) {
            report_depth();
            return error_expr(current().span);
        }
        ++depth_;
        skip_newlines();
        ast::ExprPtr lhs = parse_unary();

        while (true) {
            const std::size_t save = position_;
            skip_newlines();
            const std::optional<std::pair<ast::BinaryOp, int>> operation =
                binary_operator(current().kind);
            if (!operation.has_value() || operation->second < min_precedence) {
                position_ = save;
                break;
            }
            const NodeId node = next_id();
            const std::uint32_t start = lhs->span.start;
            advance();
            ast::ExprPtr rhs = parse_expr(operation->second + 1);
            ast::Binary binary;
            binary.id = node;
            binary.op = operation->first;
            binary.lhs = std::move(lhs);
            binary.rhs = std::move(rhs);
            binary.span = Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
            lhs = std::make_unique<ast::Expr>(
                ast::Expr{.id = node, .span = binary.span, .kind = std::move(binary)});
        }

        --depth_;
        return lhs;
    }

    ast::ExprPtr parse_unary() {
        if (at(TokenKind::KwNot)) {
            const NodeId node = next_id();
            const std::uint32_t start = current().span.start;
            advance();
            ast::Unary unary;
            unary.id = node;
            unary.op = ast::UnaryOp::Not;
            unary.operand = parse_expr(kNotPrecedence);
            unary.span = Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
            return std::make_unique<ast::Expr>(
                ast::Expr{.id = node, .span = unary.span, .kind = std::move(unary)});
        }
        if (at(TokenKind::Minus)) {
            const NodeId node = next_id();
            const std::uint32_t start = current().span.start;
            advance();
            ast::Unary unary;
            unary.id = node;
            unary.op = ast::UnaryOp::Neg;
            unary.operand = parse_expr(kUnaryPrecedence);
            unary.span = Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
            return std::make_unique<ast::Expr>(
                ast::Expr{.id = node, .span = unary.span, .kind = std::move(unary)});
        }
        return parse_postfix();
    }

    ast::ExprPtr parse_postfix() {
        ast::ExprPtr expr = parse_primary();
        while (!at_eof()) {
            const std::size_t save = position_;
            skip_newlines();
            const bool crossed_newline = position_ != save;

            if (at(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier) {
                const NodeId node = next_id();
                const std::uint32_t start = expr->span.start;
                advance();
                ast::Ident name = parse_ident("a field or method name");
                if (at(TokenKind::LParen)) {
                    ast::Method method;
                    method.id = node;
                    method.receiver = std::move(expr);
                    method.name = name;
                    method.args = parse_arguments();
                    method.trailing = maybe_trailing_lambda();
                    method.span =
                        Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
                    expr = std::make_unique<ast::Expr>(
                        ast::Expr{.id = node, .span = method.span, .kind = std::move(method)});
                } else {
                    ast::Field field;
                    field.id = node;
                    field.receiver = std::move(expr);
                    field.name = name;
                    field.optional = false;
                    field.span =
                        Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
                    expr = std::make_unique<ast::Expr>(
                        ast::Expr{.id = node, .span = field.span, .kind = std::move(field)});
                }
                continue;
            }

            if (!crossed_newline && at(TokenKind::LParen)) {
                const NodeId node = next_id();
                const std::uint32_t start = expr->span.start;
                ast::Call call;
                call.id = node;
                call.callee = std::move(expr);
                call.args = parse_arguments();
                call.trailing = maybe_trailing_lambda();
                call.span = Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
                expr = std::make_unique<ast::Expr>(
                    ast::Expr{.id = node, .span = call.span, .kind = std::move(call)});
                continue;
            }

            if (!crossed_newline && at(TokenKind::LBracket)) {
                report("E0100", "'indexing' is not supported yet (WP-07)", current().span,
                       "index expressions arrive in WP-07", std::nullopt);
                skip_construct();
                position_ = save;
                break;
            }

            position_ = save;
            break;
        }
        return expr;
    }

    ast::ExprPtr parse_number_literal(const Token& token, std::uint32_t start) {
        const NodeId node = next_id();
        ast::Lit lit;
        lit.id = node;
        lit.lit_kind = ast::LitKind::Int;
        if (token.kind == TokenKind::FloatLit) {
            lit.lit_kind = ast::LitKind::Float;
        } else if (token.kind == TokenKind::DurationLit) {
            lit.lit_kind = ast::LitKind::Duration;
        }
        lit.text = token.text;
        advance();
        const Span span = span_from(start);
        lit.span = span;
        return std::make_unique<ast::Expr>(ast::Expr{.id = node, .span = span, .kind = lit});
    }

    ast::ExprPtr parse_boolean_literal(const Token& token) {
        const NodeId node = next_id();
        ast::Lit lit;
        lit.id = node;
        lit.lit_kind = token.kind == TokenKind::KwNull ? ast::LitKind::Null : ast::LitKind::Bool;
        lit.text = token.text;
        const Span span = token.span;
        lit.span = span;
        advance();
        return std::make_unique<ast::Expr>(ast::Expr{.id = node, .span = span, .kind = lit});
    }

    ast::ExprPtr parse_keyword_primary(const Token& token, std::uint32_t start) {
        if (token.kind == TokenKind::KwIf) {
            return parse_if();
        }
        if (token.kind == TokenKind::KwAsk) {
            return parse_ask();
        }
        if (token.kind == TokenKind::KwFail) {
            return parse_fail();
        }
        if (token.kind != TokenKind::KwComplete) {
            return nullptr;
        }
        const NodeId node = next_id();
        advance();
        ast::Complete complete;
        complete.id = node;
        complete.value = parse_expr();
        complete.span = Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
        return std::make_unique<ast::Expr>(
            ast::Expr{.id = node, .span = complete.span, .kind = std::move(complete)});
    }

    ast::ExprPtr parse_identifier_primary(std::uint32_t start) {
        if (peek(1).kind == TokenKind::FatArrow) {
            return parse_simple_lambda();
        }
        const NodeId node = next_id();
        ast::Path path;
        path.id = next_id();
        path.segments.push_back(parse_ident("an identifier"));
        path.span = span_from(start);

        // `TypeName { ... }`: a named record literal. Capitalisation keeps
        // `if cond { ... }` unambiguous (see spec/grammar.ebnf notes).
        const std::string_view name = path.segments.front().text;
        const bool capitalised = !name.empty() && name.front() >= 'A' && name.front() <= 'Z';
        if (capitalised && at(TokenKind::LBrace)) {
            ast::NamedRecord named;
            named.id = node;
            named.type = std::move(path);
            named.record = parse_record_lit_value();
            named.span = Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
            return std::make_unique<ast::Expr>(
                ast::Expr{.id = node, .span = named.span, .kind = std::move(named)});
        }
        return std::make_unique<ast::Expr>(
            ast::Expr{.id = node, .span = path.span, .kind = std::move(path)});
    }

    ast::ExprPtr parse_parenthesised_primary() {
        if (is_lambda_paren()) {
            return parse_parenthesised_lambda();
        }
        advance();
        ast::ExprPtr inner = parse_expr();
        skip_newlines();
        if (at(TokenKind::Comma)) {
            report("E0100", "'tuple literals' are not supported yet (WP-07)", current().span,
                   "tuple expressions arrive in WP-07", std::nullopt);
            skip_construct();
        }
        expect(TokenKind::RParen, "')' to close the expression");
        return inner;
    }

    ast::ExprPtr parse_variant_primary(std::uint32_t start) {
        const NodeId node = next_id();
        advance();
        ast::Variant variant;
        variant.id = node;
        variant.name = parse_ident("a variant name");
        if (at(TokenKind::LParen)) {
            report("E0100", "'variant payloads' are not supported yet (WP-07)", current().span,
                   "payload variants arrive in WP-07", std::nullopt);
            skip_construct();
        }
        variant.span = span_from(start);
        return std::make_unique<ast::Expr>(
            ast::Expr{.id = node, .span = variant.span, .kind = variant});
    }

    ast::ExprPtr parse_primary() {
        const Token& token = current();
        const std::uint32_t start = token.span.start;

        if (token.kind == TokenKind::IntLit || token.kind == TokenKind::FloatLit ||
            token.kind == TokenKind::DurationLit) {
            return parse_number_literal(token, start);
        }
        if (token.kind == TokenKind::KwTrue || token.kind == TokenKind::KwFalse ||
            token.kind == TokenKind::KwNull) {
            return parse_boolean_literal(token);
        }
        if (token.kind == TokenKind::StringStart) {
            return parse_interp();
        }
        if (token.kind == TokenKind::KwIf || token.kind == TokenKind::KwAsk ||
            token.kind == TokenKind::KwFail || token.kind == TokenKind::KwComplete) {
            return parse_keyword_primary(token, start);
        }
        if (const std::optional<std::string_view> unsupported =
                unsupported_expression_keyword(token.kind);
            unsupported.has_value()) {
            report("E0100", "'" + std::string{*unsupported} + "' is not supported yet (WP-07)",
                   token.span, "'" + std::string{*unsupported} + "' arrives in WP-07",
                   std::nullopt);
            const Span span = token.span;
            skip_construct();
            return error_expr(span);
        }
        if (token.kind == TokenKind::Identifier) {
            return parse_identifier_primary(start);
        }
        if (token.kind == TokenKind::LParen) {
            return parse_parenthesised_primary();
        }
        if (token.kind == TokenKind::LBrace) {
            return parse_record_lit();
        }
        if (token.kind == TokenKind::LBracket) {
            return parse_list_lit();
        }
        if (token.kind == TokenKind::Dot && peek(1).kind == TokenKind::Identifier) {
            return parse_variant_primary(start);
        }
        if (token.kind == TokenKind::Error) {
            report("E0104", "invalid or unterminated token", token.span, "cannot tokenise this",
                   std::nullopt);
            advance();
            return error_expr(token.span);
        }

        report("E0105", "expected an expression, found " + quote_token(token), token.span,
               "expected an expression here", std::nullopt);
        return error_expr(token.span);
    }

    ast::ExprPtr parse_interp() {
        const NodeId node = next_id();
        const std::uint32_t start = current().span.start;
        ast::Interp interp;
        interp.id = node;
        interp.open = current().text;
        advance();

        bool closed = false;
        while (!at_eof()) {
            if (at(TokenKind::StringPart)) {
                interp.parts.emplace_back(current().text);
                advance();
                continue;
            }
            if (at(TokenKind::InterpStart)) {
                advance();
                if (at(TokenKind::InterpEnd)) {
                    report("E0109", "empty interpolation", current().span,
                           "`{}` has no expression inside", std::nullopt);
                    advance();
                    continue;
                }
                ast::ExprPtr part = parse_expr();
                skip_newlines();
                interp.parts.emplace_back(std::move(part));
                expect(TokenKind::InterpEnd, "'}' to close the interpolation");
                continue;
            }
            if (at(TokenKind::StringEnd)) {
                interp.close = current().text;
                advance();
                closed = true;
                break;
            }
            if (at(TokenKind::Error)) {
                break;
            }
            break;
        }

        if (!closed) {
            report("E0103", "unterminated string literal",
                   Span{.file = file_, .start = start, .end = last_end_},
                   "this string is not closed", std::nullopt);
        }
        interp.span = Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
        return std::make_unique<ast::Expr>(
            ast::Expr{.id = node, .span = interp.span, .kind = std::move(interp)});
    }

    ast::ExprPtr parse_if() {
        const NodeId node = next_id();
        const std::uint32_t start = current().span.start;
        advance();

        ast::IfExpr expr;
        expr.id = node;
        expr.cond = parse_expr();
        expr.then_block = parse_block();

        const std::size_t save = position_;
        skip_newlines();
        if (at(TokenKind::KwElse)) {
            advance();
            skip_newlines();
            if (at(TokenKind::KwIf)) {
                expr.else_branch = parse_if();
            } else {
                ast::Block block = parse_block();
                const NodeId block_id = block.id;
                expr.else_branch = std::make_unique<ast::Expr>(
                    ast::Expr{.id = block_id, .span = block.span, .kind = std::move(block)});
            }
        } else {
            position_ = save;
        }

        expr.span = Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
        return std::make_unique<ast::Expr>(
            ast::Expr{.id = node, .span = expr.span, .kind = std::move(expr)});
    }

    ast::ExprPtr parse_simple_lambda() {
        const NodeId node = next_id();
        const std::uint32_t start = current().span.start;
        ast::Lambda lambda;
        lambda.id = node;
        lambda.params.push_back(parse_ident("a lambda parameter"));
        expect(TokenKind::FatArrow, "'=>' after a lambda parameter");
        lambda.body = parse_lambda_body();
        lambda.span = Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
        return std::make_unique<ast::Expr>(
            ast::Expr{.id = node, .span = lambda.span, .kind = std::move(lambda)});
    }

    ast::ExprPtr parse_parenthesised_lambda() {
        const NodeId node = next_id();
        const std::uint32_t start = current().span.start;
        ast::Lambda lambda;
        lambda.id = node;
        lambda.parenthesised = true;
        advance(); // '('
        skip_newlines();
        while (!at(TokenKind::RParen) && !at_eof()) {
            lambda.params.push_back(parse_ident("a lambda parameter"));
            skip_newlines();
            if (!match(TokenKind::Comma)) {
                break;
            }
            skip_newlines();
        }
        expect(TokenKind::RParen, "')' to close the lambda parameters");
        expect(TokenKind::FatArrow, "'=>' after the lambda parameters");
        lambda.body = parse_lambda_body();
        lambda.span = Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
        return std::make_unique<ast::Expr>(
            ast::Expr{.id = node, .span = lambda.span, .kind = std::move(lambda)});
    }

    ast::ExprPtr parse_lambda_body() {
        if (at(TokenKind::LBrace)) {
            const NodeId node = next_id();
            const std::uint32_t start = current().span.start;
            advance();
            ast::Block block = parse_block_body(node, start);
            block.span = span_from(start);
            expect(TokenKind::RBrace, "'}' to close the lambda body");
            return std::make_unique<ast::Expr>(
                ast::Expr{.id = node, .span = block.span, .kind = std::move(block)});
        }
        return parse_expr();
    }

    ast::ExprPtr parse_ask() {
        const NodeId node = next_id();
        const std::uint32_t start = current().span.start;
        advance();
        ast::Ask ask;
        ask.id = node;
        ask.target = parse_postfix_target();
        if (!at(TokenKind::LParen)) {
            report("E0101", "expected '(' after the agent in `ask`", current().span,
                   "`ask` takes a call argument list", std::nullopt);
        } else {
            ask.args = parse_arguments();
        }
        ask.span = Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
        return std::make_unique<ast::Expr>(
            ast::Expr{.id = node, .span = ask.span, .kind = std::move(ask)});
    }

    /// A postfix chain without calls, used for the `ask` callee.
    ast::ExprPtr parse_postfix_target() {
        ast::ExprPtr expr = parse_primary();
        while (at(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier) {
            const NodeId node = next_id();
            const std::uint32_t start = expr->span.start;
            advance();
            ast::Field field;
            field.id = node;
            field.receiver = std::move(expr);
            field.name = parse_ident("a field name");
            field.span = Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
            expr = std::make_unique<ast::Expr>(
                ast::Expr{.id = node, .span = field.span, .kind = std::move(field)});
        }
        return expr;
    }

    ast::ExprPtr parse_fail() {
        const NodeId node = next_id();
        const std::uint32_t start = current().span.start;
        advance();
        ast::FailExpr fail;
        fail.id = node;
        fail.kind = parse_ident("a failure kind");
        if (at(TokenKind::LParen)) {
            advance();
            skip_newlines();
            while (!at(TokenKind::RParen) && !at_eof()) {
                fail.args.push_back(parse_expr());
                skip_newlines();
                if (!match(TokenKind::Comma)) {
                    break;
                }
                skip_newlines();
            }
            expect(TokenKind::RParen, "')' to close the failure arguments");
        }
        fail.span = Span{.file = file_, .start = start, .end = std::max(last_end_, start)};
        return std::make_unique<ast::Expr>(
            ast::Expr{.id = node, .span = fail.span, .kind = std::move(fail)});
    }

    std::vector<ast::Arg> parse_arguments() {
        std::vector<ast::Arg> args;
        expect(TokenKind::LParen, "'('");
        skip_newlines();
        while (!at(TokenKind::RParen) && !at_eof()) {
            if (current().kind == TokenKind::Identifier && peek(1).kind == TokenKind::Colon) {
                ast::NamedArg named;
                named.name = parse_ident("an argument name");
                advance(); // ':'
                named.value = parse_expr();
                args.emplace_back(std::move(named));
            } else {
                args.emplace_back(parse_expr());
            }
            skip_newlines();
            if (!match(TokenKind::Comma)) {
                break;
            }
            skip_newlines();
        }
        expect(TokenKind::RParen, "')' to close the argument list");
        return args;
    }

    std::optional<ast::ExprPtr> maybe_trailing_lambda() {
        if (!at(TokenKind::LBrace) || !is_trailing_lambda_start()) {
            return std::nullopt;
        }
        const NodeId node = next_id();
        const std::uint32_t start = current().span.start;
        advance(); // '{'
        ast::Lambda lambda;
        lambda.id = node;
        lambda.trailing = true;
        skip_newlines();
        if (at(TokenKind::LParen)) {
            lambda.parenthesised = true;
            advance();
            skip_newlines();
            while (!at(TokenKind::RParen) && !at_eof()) {
                lambda.params.push_back(parse_ident("a lambda parameter"));
                skip_newlines();
                if (!match(TokenKind::Comma)) {
                    break;
                }
                skip_newlines();
            }
            expect(TokenKind::RParen, "')' to close the lambda parameters");
        } else {
            lambda.params.push_back(parse_ident("a lambda parameter"));
        }
        expect(TokenKind::FatArrow, "'=>' in the trailing lambda");
        skip_newlines();

        ast::Block body;
        body.id = next_id();
        body = parse_block_body(body.id, start);
        body.span = span_from(start);
        expect(TokenKind::RBrace, "'}' to close the trailing lambda");
        lambda.body = std::make_unique<ast::Expr>(
            ast::Expr{.id = body.id, .span = body.span, .kind = std::move(body)});
        lambda.span = span_from(start);
        return std::make_unique<ast::Expr>(
            ast::Expr{.id = node, .span = lambda.span, .kind = std::move(lambda)});
    }

    ast::ExprPtr parse_record_lit() {
        const NodeId node = next_id();
        const std::uint32_t start = current().span.start;
        ast::RecordLit record = parse_record_lit_value();
        return std::make_unique<ast::Expr>(
            ast::Expr{.id = node, .span = span_from(start), .kind = std::move(record)});
    }

    ast::RecordLit parse_record_lit_value() {
        const NodeId node = next_id();
        const std::uint32_t start = current().span.start;
        advance(); // '{'
        ast::RecordLit record;
        record.id = node;
        skip_newlines();

        while (!at(TokenKind::RBrace) && !at_eof()) {
            ast::FieldInit field;
            field.leading = take_leading(current());
            field.name = parse_ident("a field name");
            if (match(TokenKind::Colon)) {
                field.value = parse_expr();
            }
            record.fields.push_back(std::move(field));
            skip_newlines();
            if (!match(TokenKind::Comma)) {
                break;
            }
            skip_newlines();
        }

        record.trailing = take_leading(current());
        record.span = span_from(start);
        expect(TokenKind::RBrace, "'}' to close the record literal");
        return record;
    }

    ast::ExprPtr parse_list_lit() {
        const NodeId node = next_id();
        const std::uint32_t start = current().span.start;
        advance(); // '['
        ast::ListLit list;
        list.id = node;
        skip_newlines();

        while (!at(TokenKind::RBracket) && !at_eof()) {
            list.elems.push_back(parse_expr());
            skip_newlines();
            if (!match(TokenKind::Comma)) {
                break;
            }
            skip_newlines();
        }

        list.trailing = take_leading(current());
        list.span = span_from(start);
        expect(TokenKind::RBracket, "']' to close the list");
        return std::make_unique<ast::Expr>(
            ast::Expr{.id = node, .span = list.span, .kind = std::move(list)});
    }

    ast::ExprPtr error_expr(Span span) {
        const NodeId node = next_id();
        ast::Lit lit;
        lit.id = node;
        lit.lit_kind = ast::LitKind::Null;
        lit.text = "null";
        lit.span = span;
        return std::make_unique<ast::Expr>(ast::Expr{.id = node, .span = span, .kind = lit});
    }

    // ------------------------------------------------------------------- types

    ast::TypeExprPtr parse_type() {
        if (depth_ >= kMaxDepth) {
            report_depth();
            return error_type(current().span);
        }
        ++depth_;

        const NodeId node = next_id();
        const std::uint32_t start = current().span.start;
        ast::TypeExprPtr result;

        if (at(TokenKind::LBracket)) {
            advance();
            ast::ListType list;
            list.elem = parse_type();
            expect(TokenKind::RBracket, "']' to close the list type");
            result = std::make_unique<ast::TypeExpr>(
                ast::TypeExpr{.id = node, .span = span_from(start), .kind = std::move(list)});
        } else if (at(TokenKind::LBrace)) {
            ast::RecordType record = parse_record_type();
            result = std::make_unique<ast::TypeExpr>(
                ast::TypeExpr{.id = node, .span = span_from(start), .kind = std::move(record)});
        } else if (at(TokenKind::KwFn)) {
            report("E0100", "'function types' are not supported yet (WP-07)", current().span,
                   "function types arrive in WP-07", std::nullopt);
            skip_construct();
            result = error_type(current().span);
        } else if (at(TokenKind::LParen)) {
            advance();
            result = parse_type();
            expect(TokenKind::RParen, "')' to close the type");
        } else {
            ast::NamedType named;
            named.path = parse_path();
            result = std::make_unique<ast::TypeExpr>(
                ast::TypeExpr{.id = node, .span = span_from(start), .kind = std::move(named)});
        }

        if (at(TokenKind::Question)) {
            advance();
            ast::OptionalType optional;
            optional.inner = std::move(result);
            result = std::make_unique<ast::TypeExpr>(
                ast::TypeExpr{.id = node, .span = span_from(start), .kind = std::move(optional)});
        }

        --depth_;
        return result;
    }

    ast::TypeExprPtr error_type(Span span) {
        const NodeId node = next_id();
        ast::NamedType named;
        named.path.id = next_id();
        named.path.span = span;
        named.path.segments.push_back(ast::Ident{.text = "Unknown", .span = span});
        return std::make_unique<ast::TypeExpr>(
            ast::TypeExpr{.id = node, .span = span, .kind = std::move(named)});
    }

    ast::RecordType parse_record_type() {
        expect(TokenKind::LBrace, "'{' to start a record type");
        ast::RecordType record;
        skip_newlines();

        while (!at(TokenKind::RBrace) && !at_eof()) {
            ast::RecordField field;
            field.leading = take_leading(current());
            field.name = parse_ident("a field name");
            expect(TokenKind::Colon, "':' after a record field name");
            field.type = parse_type();
            if (match(TokenKind::KwWhere)) {
                field.refine = parse_expr();
            }
            record.fields.push_back(std::move(field));
            skip_newlines();
            if (!match(TokenKind::Comma)) {
                break;
            }
            skip_newlines();
        }

        record.trailing = take_leading(current());
        expect(TokenKind::RBrace, "'}' to close the record type");
        return record;
    }

    // ---------------------------------------------------------------- lookahead

    [[nodiscard]] bool is_lambda_paren() const {
        if (!at(TokenKind::LParen)) {
            return false;
        }
        std::size_t index = position_;
        int depth = 0;
        while (index < tokens_.size()) {
            const TokenKind kind = tokens_.at(index).kind;
            if (kind == TokenKind::LParen) {
                ++depth;
            } else if (kind == TokenKind::RParen) {
                --depth;
                if (depth == 0) {
                    ++index;
                    break;
                }
            } else if (kind == TokenKind::Eof) {
                return false;
            }
            ++index;
        }
        return index < tokens_.size() && tokens_.at(index).kind == TokenKind::FatArrow;
    }

    [[nodiscard]] bool is_trailing_lambda_start() const {
        if (!at(TokenKind::LBrace)) {
            return false;
        }
        std::size_t index = position_ + 1;
        while (index < tokens_.size() && tokens_.at(index).kind == TokenKind::Newline) {
            ++index;
        }
        if (index >= tokens_.size()) {
            return false;
        }
        if (tokens_.at(index).kind == TokenKind::Identifier &&
            kind_at(index + 1) == TokenKind::FatArrow) {
            return true;
        }
        if (tokens_.at(index).kind != TokenKind::LParen) {
            return false;
        }
        int depth = 0;
        while (index < tokens_.size()) {
            const TokenKind kind = tokens_.at(index).kind;
            if (kind == TokenKind::LParen) {
                ++depth;
            } else if (kind == TokenKind::RParen) {
                --depth;
                if (depth == 0) {
                    ++index;
                    break;
                }
            } else if (kind == TokenKind::Eof) {
                return false;
            }
            ++index;
        }
        while (index < tokens_.size() && tokens_.at(index).kind == TokenKind::Newline) {
            ++index;
        }
        return index < tokens_.size() && tokens_.at(index).kind == TokenKind::FatArrow;
    }

    // -------------------------------------------------------------- identifiers

    ast::Ident parse_ident(std::string_view what) {
        if (current().kind == TokenKind::Identifier) {
            const Token& token = advance();
            return ast::Ident{.text = token.text, .span = token.span};
        }
        report("E0101", "expected " + std::string{what} + ", found " + quote_token(current()),
               current().span, "expected " + std::string{what} + " here", std::nullopt);
        return ast::Ident{.text = "<error>", .span = current().span};
    }

    ast::Path parse_path() {
        ast::Path path;
        path.id = next_id();
        const std::uint32_t start = current().span.start;
        path.segments.push_back(parse_ident("a path segment"));
        while (at(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier) {
            advance();
            path.segments.push_back(parse_ident("a path segment"));
        }
        path.span = span_from(start);
        return path;
    }

    std::string_view source_;
    FileId file_;
    std::vector<Token> tokens_;
    std::size_t position_ = 0;
    std::uint32_t last_end_ = 0;
    std::uint32_t next_node_ = 0;
    int depth_ = 0;
    bool depth_reported_ = false;
    std::vector<Diagnostic> diagnostics_;
};

} // namespace

Parsed parse_program(std::string_view src, FileId file) { return Parser(src, file).run(); }

} // namespace leash::syntax
