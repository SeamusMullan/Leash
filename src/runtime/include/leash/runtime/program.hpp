// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

// MIGRATION (WP-10): this is the evaluator's input model for v0. The evaluator
// and every runtime source depend only on this header, never on the syntax AST.
// WP-10 replaces `from_ast` with `from_lir` and the node types here with the
// lowered LIR shape; keep that swap confined to program.cpp/program.hpp.

#include <chrono>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "leash/runtime/failure.hpp"
#include "leash/syntax/span.hpp"

namespace leash::syntax::ast {
struct Program;
} // namespace leash::syntax::ast

namespace leash::runtime {

// ---------------------------------------------------------------------- types

struct TypeRef;
using TypeRefPtr = std::shared_ptr<const TypeRef>;

/// The v0 type surface. `Unknown` means "no annotation"; the evaluator does
/// not enforce annotations (WP-08 is the checker), it only carries them for
/// schema generation and for the future LIR.
struct TypeRef {
    enum class Kind : std::uint8_t {
        Unknown,
        Unit,
        Bool,
        Int,
        Float,
        Str,
        Duration,
        Money,
        Json,
        Named,
        List,
        Optional,
        Record,
    };

    Kind kind = Kind::Unknown;
    std::string name;                                       // Named, nominal Record
    TypeRefPtr element;                                     // List element, Optional inner
    std::vector<std::pair<std::string, TypeRefPtr>> fields; // Record

    [[nodiscard]] bool is_record() const noexcept { return kind == Kind::Record; }
};

[[nodiscard]] bool same_type(const TypeRef& lhs, const TypeRef& rhs);
[[nodiscard]] std::string type_name(const TypeRef& type);

// ---------------------------------------------------------------- statements

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct LetStmt {
    std::string name;
    TypeRefPtr type;
    ExprPtr value;
};

struct ExprStmt {
    ExprPtr expr;
};

struct Stmt {
    syntax::Span span;
    std::variant<LetStmt, ExprStmt> node;
};

struct Block {
    syntax::Span span;
    std::vector<Stmt> statements;
    ExprPtr tail; // null means Unit
};

// --------------------------------------------------------------- expressions

struct UnitLit {};
struct NullLit {};
struct BoolLit {
    bool value = false;
};
struct IntLit {
    std::int64_t value = 0;
};
struct FloatLit {
    double value = 0.0;
};
struct DurationLit {
    std::chrono::nanoseconds value{};
};

/// One literal slice or one `{expr}` interpolation inside a string.
using StringPart = std::variant<std::string, ExprPtr>;

struct StringLit {
    std::vector<StringPart> parts;
    bool raw = false; // `r"..."`: backslashes and braces are literal
};

struct RecordFieldValue {
    std::string name;
    ExprPtr value;
};

struct ListExpr {
    std::vector<ExprPtr> elements;
};

struct RecordExpr {
    std::string type; // empty for a structural literal, `Name { ... }` otherwise
    std::vector<RecordFieldValue> fields;
};

struct EnumLit {
    std::string type; // empty when the literal is `.Name`
    std::string variant;
    std::vector<ExprPtr> payload;
};

struct NameExpr {
    std::vector<std::string> path;
};

struct IfExpr {
    ExprPtr cond;
    Block then_block;
    ExprPtr else_branch; // null means Unit
};

enum class UnaryOperator : std::uint8_t { Negate, Not };

struct UnaryExpr {
    UnaryOperator op = UnaryOperator::Negate;
    ExprPtr operand;
};

enum class BinaryOperator : std::uint8_t {
    Or,
    And,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
};

struct BinaryExpr {
    BinaryOperator op = BinaryOperator::Add;
    ExprPtr lhs;
    ExprPtr rhs;
};

struct CallArg {
    std::optional<std::string> name;
    ExprPtr value;
};

struct CallExpr {
    ExprPtr callee;
    std::vector<CallArg> args;
};

struct MethodExpr {
    ExprPtr receiver;
    std::string name;
    std::vector<CallArg> args;
};

struct FieldExpr {
    ExprPtr receiver;
    std::string name;
    bool optional = false;
};

struct LambdaExpr {
    std::vector<std::string> params;
    ExprPtr body;
};

struct AskExpr {
    ExprPtr target;
    std::vector<CallArg> args;
};

struct FailExpr {
    std::string kind;
    std::vector<ExprPtr> args;
};

struct CompleteExpr {
    ExprPtr value;
};

using ExprNode =
    std::variant<UnitLit, NullLit, BoolLit, IntLit, FloatLit, DurationLit, StringLit, ListExpr,
                 RecordExpr, EnumLit, NameExpr, IfExpr, Block, UnaryExpr, BinaryExpr, CallExpr,
                 MethodExpr, FieldExpr, LambdaExpr, AskExpr, FailExpr, CompleteExpr>;

struct Expr {
    syntax::Span span;
    ExprNode node;
};

// ---------------------------------------------------------------------- items

struct Param {
    std::string name;
    TypeRefPtr type;
    ExprPtr default_value;
};

/// A function declaration. Named `FnDecl` because `runtime::Fn` is the value
/// alternative in value.hpp; the two cannot share one namespace.
struct FnDecl {
    std::string name;
    std::vector<Param> params;
    TypeRefPtr result;
    Block body;
};

struct Flow {
    std::string name;
    Param param;
    TypeRefPtr result;
    Block body;
};

struct Agent {
    std::string name;
    ExprPtr value;
};

struct Task {
    std::string name;
    std::optional<TypeRefPtr> input;
    TypeRefPtr output;
    ExprPtr budget;
};

struct Const {
    std::string name;
    TypeRefPtr type;
    ExprPtr value;
};

struct Program {
    std::string module;
    std::vector<Const> consts;
    std::vector<FnDecl> fns;
    std::vector<Flow> flows;
    std::vector<Agent> agents;
    std::vector<Task> tasks;
    std::map<std::string, TypeRefPtr, std::less<>> types;

    [[nodiscard]] const FnDecl* find_fn(std::string_view name) const noexcept;
    [[nodiscard]] const Flow* find_flow(std::string_view name) const noexcept;
    [[nodiscard]] const Const* find_const(std::string_view name) const noexcept;
    [[nodiscard]] const Agent* find_agent(std::string_view name) const noexcept;
    [[nodiscard]] const Task* find_task(std::string_view name) const noexcept;
};

/// Name for diagnostics; never null.
[[nodiscard]] std::string_view expr_kind_name(const Expr& expr);
[[nodiscard]] std::string_view stmt_kind_name(const Stmt& stmt) noexcept;

/// Lower the v0 syntax AST into the evaluator's model. Unsupported constructs
/// become an `Unknown` failure naming the construct; v0 accepts every
/// expression the WP-03 parser produces.
[[nodiscard]] std::expected<Program, Failure> from_ast(const syntax::ast::Program& ast);

} // namespace leash::runtime
