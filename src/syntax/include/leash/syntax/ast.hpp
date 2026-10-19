// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "leash/syntax/span.hpp"

namespace leash::syntax::ast {

/// A comment preserved from the source. Doc comments (`///`) are program
/// content and attach to the item that follows them; the other kinds are
/// trivia that the formatter re-emits.
struct Comment {
    enum class Kind : std::uint8_t { Line, Block, Doc };

    Kind kind = Kind::Line;
    Span span;
    std::string_view text; // delimiters included, trailing newline excluded
};

/// Comments and blank-line information preceding a node.
struct Leading {
    std::vector<Comment> comments;
    bool blank_before = false;

    [[nodiscard]] bool empty() const { return comments.empty(); }
};

struct Ident {
    std::string_view text;
    Span span;

    bool operator==(const Ident& other) const { return text == other.text; }
};

struct Path {
    NodeId id;
    Span span;
    std::vector<Ident> segments;

    [[nodiscard]] std::string_view last() const {
        return segments.empty() ? std::string_view{} : segments.back().text;
    }

    bool operator==(const Path& other) const { return segments == other.segments; }
};

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct TypeExpr;
using TypeExprPtr = std::unique_ptr<TypeExpr>;

// ---------------------------------------------------------------- expressions

enum class LitKind : std::uint8_t { Int, Float, Duration, Bool, Null };

struct Lit {
    NodeId id;
    Span span;
    LitKind lit_kind = LitKind::Int;
    std::string_view text;

    bool operator==(const Lit& other) const {
        return lit_kind == other.lit_kind && text == other.text;
    }
};

struct FieldInit {
    Ident name;
    std::optional<ExprPtr> value; // nullopt is `{ name }` shorthand
    Leading leading;

    [[nodiscard]] bool shorthand() const { return !value.has_value(); }
};

struct RecordLit {
    NodeId id;
    Span span;
    std::vector<FieldInit> fields;
    Leading trailing;

    bool operator==(const RecordLit& other) const;
};

struct NamedRecord {
    NodeId id;
    Span span;
    Path type;
    RecordLit record;

    bool operator==(const NamedRecord& other) const;
};

struct ListLit {
    NodeId id;
    Span span;
    std::vector<ExprPtr> elems;
    Leading trailing;

    bool operator==(const ListLit& other) const;
};

struct Variant {
    NodeId id;
    Span span;
    Ident name;

    bool operator==(const Variant& other) const { return name == other.name; }
};

// ----------------------------------------------------------------- statements

struct LetStmt {
    NodeId id;
    Span span;
    Leading leading;
    Ident name;
    TypeExprPtr type; // optional annotation
    ExprPtr value;

    bool operator==(const LetStmt& other) const;
};

struct ExprStmt {
    NodeId id;
    Span span;
    Leading leading;
    ExprPtr expr;

    bool operator==(const ExprStmt& other) const;
};

using Stmt = std::variant<LetStmt, ExprStmt>;

struct Block {
    NodeId id;
    Span span;
    std::vector<Stmt> stmts;
    std::optional<ExprPtr> tail; // trailing expression without a newline
    Leading trailing;

    bool operator==(const Block& other) const;
};

struct IfExpr {
    NodeId id;
    Span span;
    ExprPtr cond;
    Block then_block;
    std::optional<ExprPtr> else_branch; // IfExpr or Block

    bool operator==(const IfExpr& other) const;
};

enum class UnaryOp : std::uint8_t { Neg, Not };

struct Unary {
    NodeId id;
    Span span;
    UnaryOp op = UnaryOp::Neg;
    ExprPtr operand;

    bool operator==(const Unary& other) const;
};

enum class BinaryOp : std::uint8_t {
    Or,
    And,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    Add,
    Sub,
    Mul,
    Div,
    Rem,
};

struct Binary {
    NodeId id;
    Span span;
    BinaryOp op = BinaryOp::Add;
    ExprPtr lhs;
    ExprPtr rhs;

    bool operator==(const Binary& other) const;
};

struct NamedArg {
    Ident name;
    ExprPtr value;

    bool operator==(const NamedArg& other) const;
};

/// Positional arguments are `ExprPtr`; named arguments are `NamedArg`.
using Arg = std::variant<ExprPtr, NamedArg>;

struct Call {
    NodeId id;
    Span span;
    ExprPtr callee;
    std::vector<Arg> args;
    std::optional<ExprPtr> trailing; // trailing lambda, already parsed as Lambda

    bool operator==(const Call& other) const;
};

struct Method {
    NodeId id;
    Span span;
    ExprPtr receiver;
    Ident name;
    std::vector<Arg> args;
    std::optional<ExprPtr> trailing;

    bool operator==(const Method& other) const;
};

struct Field {
    NodeId id;
    Span span;
    ExprPtr receiver;
    Ident name;
    bool optional = false; // `receiver?.name`

    bool operator==(const Field& other) const;
};

struct Lambda {
    NodeId id;
    Span span;
    std::vector<Ident> params;
    bool parenthesised = false;
    bool trailing = false; // `{ x => ... }` attached to a call
    ExprPtr body;          // a Block node for `x => { ... }`

    bool operator==(const Lambda& other) const;
};

struct Ask {
    NodeId id;
    Span span;
    ExprPtr target;
    std::vector<Arg> args;

    bool operator==(const Ask& other) const;
};

struct FailExpr {
    NodeId id;
    Span span;
    Ident kind;
    std::vector<ExprPtr> args;

    bool operator==(const FailExpr& other) const;
};

struct Complete {
    NodeId id;
    Span span;
    ExprPtr value;

    bool operator==(const Complete& other) const;
};

/// A string literal. All strings are `Interp`, including strings without
/// substitutions, so the formatter can reproduce delimiters, escapes and
/// `{{`/`}}` verbatim. `parts` alternates raw text slices and expressions.
using InterpPart = std::variant<std::string_view, ExprPtr>;

struct Interp {
    NodeId id;
    Span span;
    std::string_view open; // `"`, `"""`, `r"` or `r"""`
    std::vector<InterpPart> parts;
    std::string_view close;

    bool operator==(const Interp& other) const;
};

struct Expr {
    NodeId id;
    Span span;
    std::variant<Lit, Path, RecordLit, NamedRecord, ListLit, Variant, IfExpr, Block, Unary, Binary,
                 Call, Method, Field, Lambda, Ask, FailExpr, Complete, Interp>
        kind;

    bool operator==(const Expr& other) const { return kind == other.kind; }
};

// ---------------------------------------------------------------------- types

struct NamedType {
    Path path;

    bool operator==(const NamedType& other) const { return path == other.path; }
};

struct ListType {
    TypeExprPtr elem;

    bool operator==(const ListType& other) const;
};

struct OptionalType {
    TypeExprPtr inner;

    bool operator==(const OptionalType& other) const;
};

struct RecordField {
    Ident name;
    TypeExprPtr type;
    std::optional<ExprPtr> refine; // `where <expr>`
    Leading leading;

    bool operator==(const RecordField& other) const;
};

struct RecordType {
    std::vector<RecordField> fields;
    Leading trailing;

    bool operator==(const RecordType& other) const;
};

struct TypeExpr {
    NodeId id;
    Span span;
    std::variant<NamedType, ListType, OptionalType, RecordType> kind;

    bool operator==(const TypeExpr& other) const { return kind == other.kind; }
};

// ---------------------------------------------------------------------- items

struct UseDecl {
    NodeId id;
    Span span;
    Leading leading;
    Path path;
    bool grouped = false;
    std::vector<Ident> names;

    bool operator==(const UseDecl& other) const;
};

struct ConstDecl {
    NodeId id;
    Span span;
    Leading leading;
    bool is_pub = false;
    Ident name;
    TypeExprPtr type;
    ExprPtr value;

    bool operator==(const ConstDecl& other) const;
};

struct TypeDecl {
    NodeId id;
    Span span;
    Leading leading;
    bool is_pub = false;
    Ident name;
    TypeExprPtr type;

    bool operator==(const TypeDecl& other) const;
};

struct Param {
    Ident name;
    TypeExprPtr type;
    ExprPtr default_value;
    Leading leading;

    bool operator==(const Param& other) const;
};

struct EffectClause {
    Span span;
    std::vector<Path> effects;

    bool operator==(const EffectClause& other) const { return effects == other.effects; }
};

struct FnDecl {
    NodeId id;
    Span span;
    Leading leading;
    bool is_pub = false;
    Ident name;
    std::vector<Param> params;
    TypeExprPtr ret;
    std::optional<EffectClause> effects;
    Block body;

    bool operator==(const FnDecl& other) const;
};

struct AgentDecl {
    NodeId id;
    Span span;
    Leading leading;
    bool is_pub = false;
    Ident name;
    ExprPtr value;

    bool operator==(const AgentDecl& other) const;
};

struct TaskInput {
    NodeId id;
    Span span;
    Leading leading;
    RecordType type;

    bool operator==(const TaskInput& other) const { return type == other.type; }
};

struct TaskOutput {
    NodeId id;
    Span span;
    Leading leading;
    TypeExprPtr type;

    bool operator==(const TaskOutput& other) const;
};

struct TaskBudget {
    NodeId id;
    Span span;
    Leading leading;
    ExprPtr value;

    bool operator==(const TaskBudget& other) const;
};

using TaskSection = std::variant<TaskInput, TaskOutput, TaskBudget>;

struct TaskDecl {
    NodeId id;
    Span span;
    Leading leading;
    bool is_pub = false;
    Ident name;
    std::vector<TaskSection> sections;

    bool operator==(const TaskDecl& other) const;
};

struct FlowDecl {
    NodeId id;
    Span span;
    Leading leading;
    bool is_pub = false;
    Ident name;
    Param param;
    TypeExprPtr ret;
    std::optional<EffectClause> effects;
    Block body;

    bool operator==(const FlowDecl& other) const;
};

using Item = std::variant<UseDecl, ConstDecl, TypeDecl, FnDecl, AgentDecl, TaskDecl, FlowDecl>;

struct Program {
    std::optional<Path> module_path;
    std::vector<Item> items;
    NodeId id;
    Span span;
    Leading leading;
    Leading trailing;

    bool operator==(const Program& other) const;
};

// ------------------------------------------------------- uniform node access

[[nodiscard]] NodeId node_id(const Item& item);
[[nodiscard]] Span node_span(const Item& item);
[[nodiscard]] NodeId node_id(const Expr& expr);
[[nodiscard]] Span node_span(const Expr& expr);
[[nodiscard]] NodeId node_id(const TypeExpr& type);
[[nodiscard]] Span node_span(const TypeExpr& type);
[[nodiscard]] NodeId node_id(const Stmt& stmt);
[[nodiscard]] Span node_span(const Stmt& stmt);
[[nodiscard]] NodeId node_id(const Program& program);

} // namespace leash::syntax::ast
