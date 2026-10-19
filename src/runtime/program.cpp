// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/runtime/program.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <ranges>
#include <string>
#include <utility>

#include "leash/syntax/ast.hpp"

namespace leash::runtime {
namespace {

using syntax::ast::BinaryOp;
using syntax::ast::Item;
using syntax::ast::LitKind;
using syntax::ast::TypeExpr;
using syntax::ast::UnaryOp;

template <typename T> using Result = std::expected<T, Failure>;

[[nodiscard]] Failure make_failure(std::string message, std::string_view item = {}) {
    return Failure{
        .kind = spec::FailureKind::Unknown,
        .message = std::move(message),
        .data = std::nullopt,
        .site = Site{.module = {}, .item = std::string{item}, .node_kind = {}, .line = 0},
        .cause = nullptr,
    };
}

[[nodiscard]] std::string join_path(const syntax::ast::Path& path) {
    std::string out;
    for (const syntax::ast::Ident& segment : path.segments) {
        if (!out.empty()) {
            out.push_back('.');
        }
        out.append(segment.text);
    }
    return out;
}

[[nodiscard]] bool is_raw_string(const syntax::ast::Interp& interp) {
    return !interp.open.empty() && interp.open.front() == 'r';
}

/// Decode the escapes of a non-raw string part and collapse `{{`/`}}`.
[[nodiscard]] std::string decode_string_part(std::string_view text, bool raw) {
    if (raw) {
        return std::string{text};
    }
    std::string out;
    out.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char character = text.at(index);
        if (character != '\\') {
            if (character == '{' && index + 1 < text.size() && text.at(index + 1) == '{') {
                out.push_back('{');
                ++index;
                continue;
            }
            if (character == '}' && index + 1 < text.size() && text.at(index + 1) == '}') {
                out.push_back('}');
                ++index;
                continue;
            }
            out.push_back(character);
            continue;
        }
        if (index + 1 >= text.size()) {
            out.push_back('\\');
            break;
        }
        const char escaped = text.at(++index);
        switch (escaped) {
        case 'n':
            out.push_back('\n');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case '\\':
            out.push_back('\\');
            break;
        case '"':
            out.push_back('"');
            break;
        case '{':
            out.push_back('{');
            break;
        case '}':
            out.push_back('}');
            break;
        case '0':
            out.push_back('\0');
            break;
        default:
            out.push_back('\\');
            out.push_back(escaped);
            break;
        }
    }
    return out;
}

[[nodiscard]] std::string strip_underscores(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char character : text) {
        if (character != '_') {
            out.push_back(character);
        }
    }
    return out;
}

[[nodiscard]] Result<std::int64_t> parse_int(std::string_view text) {
    const std::string digits = strip_underscores(text);
    std::int64_t value = 0;
    const auto [ptr, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc{} || ptr != digits.data() + digits.size()) {
        return std::unexpected<Failure>{
            make_failure("invalid integer literal '" + std::string{text} + "'")};
    }
    return value;
}

[[nodiscard]] Result<double> parse_float(std::string_view text) {
    const std::string digits = strip_underscores(text);
    double value = 0.0;
    const auto [ptr, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc{} || ptr != digits.data() + digits.size() || !std::isfinite(value)) {
        return std::unexpected<Failure>{
            make_failure("invalid float literal '" + std::string{text} + "'")};
    }
    return value;
}

[[nodiscard]] Result<std::chrono::nanoseconds> parse_duration(std::string_view text) {
    std::size_t digits = 0;
    while (digits < text.size() && (text.at(digits) >= '0' && text.at(digits) <= '9')) {
        ++digits;
    }
    if (digits == 0) {
        return std::unexpected<Failure>{
            make_failure("invalid duration literal '" + std::string{text} + "'")};
    }
    const std::string number{text.substr(0, digits)};
    std::int64_t amount = 0;
    const auto [ptr, error] = std::from_chars(number.data(), number.data() + number.size(), amount);
    if (error != std::errc{} || ptr != number.data() + number.size()) {
        return std::unexpected<Failure>{
            make_failure("invalid duration literal '" + std::string{text} + "'")};
    }
    const std::string_view unit = text.substr(digits);
    std::int64_t multiplier = 0;
    if (unit == "ns") {
        multiplier = 1;
    } else if (unit == "ms") {
        multiplier = 1'000'000;
    } else if (unit == "s") {
        multiplier = 1'000'000'000;
    } else if (unit == "m") {
        multiplier = 60LL * 1'000'000'000LL;
    } else if (unit == "h") {
        multiplier = 3600LL * 1'000'000'000LL;
    } else {
        return std::unexpected<Failure>{
            make_failure("invalid duration unit '" + std::string{unit} + "'")};
    }
    return std::chrono::nanoseconds{amount * multiplier};
}

class Lowerer {
  public:
    explicit Lowerer(std::string module) : module_(std::move(module)) {}

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    [[nodiscard]] Result<Program> lower(const syntax::ast::Program& ast) {
        Program program;
        program.module = module_;

        for (const Item& item : ast.items) {
            if (const auto* declaration = std::get_if<syntax::ast::TypeDecl>(&item)) {
                auto type = lower_type(*declaration->type);
                if (!type) {
                    return std::unexpected(std::move(type.error()));
                }
                if ((*type)->kind == TypeRef::Kind::Record && (*type)->name.empty()) {
                    auto named = std::make_shared<TypeRef>(**type);
                    named->name = std::string{declaration->name.text};
                    *type = named;
                }
                program.types.emplace(std::string{declaration->name.text}, *type);
                continue;
            }
            if (const auto* declaration = std::get_if<syntax::ast::ConstDecl>(&item)) {
                auto value = lower_expr(*declaration->value);
                if (!value) {
                    return std::unexpected(std::move(value.error()));
                }
                auto type = declaration->type ? lower_type(*declaration->type)
                                              : Result<TypeRefPtr>{nullptr};
                if (!type) {
                    return std::unexpected(std::move(type.error()));
                }
                program.consts.push_back(Const{
                    .name = std::string{declaration->name.text},
                    .type = *type,
                    .value = std::move(*value),
                });
                continue;
            }
            if (const auto* declaration = std::get_if<syntax::ast::FnDecl>(&item)) {
                auto function_decl = lower_fn(*declaration);
                if (!function_decl) {
                    return std::unexpected(std::move(function_decl.error()));
                }
                program.fns.push_back(std::move(*function_decl));
                continue;
            }
            if (const auto* declaration = std::get_if<syntax::ast::AgentDecl>(&item)) {
                auto value = lower_expr(*declaration->value);
                if (!value) {
                    return std::unexpected(std::move(value.error()));
                }
                program.agents.push_back(
                    Agent{.name = std::string{declaration->name.text}, .value = std::move(*value)});
                continue;
            }
            if (const auto* declaration = std::get_if<syntax::ast::TaskDecl>(&item)) {
                auto task = lower_task(*declaration);
                if (!task) {
                    return std::unexpected(std::move(task.error()));
                }
                program.tasks.push_back(std::move(*task));
                continue;
            }
            if (const auto* declaration = std::get_if<syntax::ast::FlowDecl>(&item)) {
                auto flow = lower_flow(*declaration);
                if (!flow) {
                    return std::unexpected(std::move(flow.error()));
                }
                program.flows.push_back(std::move(*flow));
                continue;
            }
            // `use` declarations do not reach the evaluator in v0.
        }
        return program;
    }

  private:
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    [[nodiscard]] Result<ExprPtr> lower_expr(const syntax::ast::Expr& expr) {
        const syntax::Span span = expr.span;
        return std::visit(
            // NOLINTNEXTLINE(readability-function-cognitive-complexity)
            [&](const auto& node) -> Result<ExprPtr> {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, syntax::ast::Lit>) {
                    return lower_lit(node, span);
                } else if constexpr (std::is_same_v<Node, syntax::ast::Path>) {
                    return make_expr(span, NameExpr{.path = split_path(node)});
                } else if constexpr (std::is_same_v<Node, syntax::ast::RecordLit>) {
                    return lower_record(node, span, {});
                } else if constexpr (std::is_same_v<Node, syntax::ast::NamedRecord>) {
                    return lower_record(node.record, span, join_path(node.type));
                } else if constexpr (std::is_same_v<Node, syntax::ast::ListLit>) {
                    return lower_list(node, span);
                } else if constexpr (std::is_same_v<Node, syntax::ast::Variant>) {
                    return make_expr(
                        span,
                        EnumLit{.type = {}, .variant = std::string{node.name.text}, .payload = {}});
                } else if constexpr (std::is_same_v<Node, syntax::ast::IfExpr>) {
                    return lower_if(node, span);
                } else if constexpr (std::is_same_v<Node, syntax::ast::Block>) {
                    auto block = lower_block(node);
                    if (!block) {
                        return std::unexpected(std::move(block.error()));
                    }
                    return make_expr(span, std::move(*block));
                } else if constexpr (std::is_same_v<Node, syntax::ast::Unary>) {
                    auto operand = lower_expr(*node.operand);
                    if (!operand) {
                        return std::unexpected(std::move(operand.error()));
                    }
                    return make_expr(span, UnaryExpr{
                                               .op = lower_unary(node.op),
                                               .operand = std::move(*operand),
                                           });
                } else if constexpr (std::is_same_v<Node, syntax::ast::Binary>) {
                    return lower_binary(node, span);
                } else if constexpr (std::is_same_v<Node, syntax::ast::Call>) {
                    return lower_call(node, span);
                } else if constexpr (std::is_same_v<Node, syntax::ast::Method>) {
                    return lower_method(node, span);
                } else if constexpr (std::is_same_v<Node, syntax::ast::Field>) {
                    auto receiver = lower_expr(*node.receiver);
                    if (!receiver) {
                        return std::unexpected(std::move(receiver.error()));
                    }
                    return make_expr(span, FieldExpr{
                                               .receiver = std::move(*receiver),
                                               .name = std::string{node.name.text},
                                               .optional = node.optional,
                                           });
                } else if constexpr (std::is_same_v<Node, syntax::ast::Lambda>) {
                    return lower_lambda(node, span);
                } else if constexpr (std::is_same_v<Node, syntax::ast::Ask>) {
                    return lower_ask(node, span);
                } else if constexpr (std::is_same_v<Node, syntax::ast::FailExpr>) {
                    return lower_fail(node, span);
                } else if constexpr (std::is_same_v<Node, syntax::ast::Complete>) {
                    auto value = lower_expr(*node.value);
                    if (!value) {
                        return std::unexpected(std::move(value.error()));
                    }
                    return make_expr(span, CompleteExpr{.value = std::move(*value)});
                } else if constexpr (std::is_same_v<Node, syntax::ast::Interp>) {
                    return lower_interp(node, span);
                } else {
                    return std::unexpected<Failure>{make_failure("unsupported expression")};
                }
            },
            expr.kind);
    }

    [[nodiscard]] static ExprPtr make_expr(syntax::Span span, ExprNode node) {
        return std::make_unique<Expr>(Expr{.span = span, .node = std::move(node)});
    }

    [[nodiscard]] static std::vector<std::string> split_path(const syntax::ast::Path& path) {
        std::vector<std::string> segments;
        segments.reserve(path.segments.size());
        for (const syntax::ast::Ident& segment : path.segments) {
            segments.emplace_back(segment.text);
        }
        return segments;
    }

    [[nodiscard]] static Result<ExprPtr> lower_lit(const syntax::ast::Lit& lit, syntax::Span span) {
        switch (lit.lit_kind) {
        case LitKind::Bool:
            return make_expr(span, BoolLit{.value = lit.text == "true"});
        case LitKind::Null:
            return make_expr(span, NullLit{});
        case LitKind::Int: {
            auto value = parse_int(lit.text);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            return make_expr(span, IntLit{.value = *value});
        }
        case LitKind::Float: {
            auto value = parse_float(lit.text);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            return make_expr(span, FloatLit{.value = *value});
        }
        case LitKind::Duration: {
            auto value = parse_duration(lit.text);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            return make_expr(span, DurationLit{.value = *value});
        }
        }
        return std::unexpected<Failure>{make_failure("unsupported literal")};
    }

    [[nodiscard]] Result<ExprPtr> lower_record(const syntax::ast::RecordLit& record,
                                               syntax::Span span, std::string type) {
        RecordExpr lowered;
        lowered.type = std::move(type);
        lowered.fields.reserve(record.fields.size());
        for (const syntax::ast::FieldInit& field : record.fields) {
            if (field.value) {
                auto value = lower_expr(**field.value);
                if (!value) {
                    return std::unexpected(std::move(value.error()));
                }
                lowered.fields.push_back(RecordFieldValue{
                    .name = std::string{field.name.text},
                    .value = std::move(*value),
                });
            } else {
                lowered.fields.push_back(RecordFieldValue{
                    .name = std::string{field.name.text},
                    .value = make_expr(field.name.span,
                                       NameExpr{.path = {std::string{field.name.text}}}),
                });
            }
        }
        return make_expr(span, std::move(lowered));
    }

    [[nodiscard]] Result<ExprPtr> lower_list(const syntax::ast::ListLit& list, syntax::Span span) {
        ListExpr lowered;
        lowered.elements.reserve(list.elems.size());
        for (const auto& element : list.elems) {
            auto value = lower_expr(*element);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            lowered.elements.push_back(std::move(*value));
        }
        return make_expr(span, std::move(lowered));
    }

    [[nodiscard]] Result<ExprPtr> lower_if(const syntax::ast::IfExpr& expr, syntax::Span span) {
        auto cond = lower_expr(*expr.cond);
        if (!cond) {
            return std::unexpected(std::move(cond.error()));
        }
        auto then_block = lower_block(expr.then_block);
        if (!then_block) {
            return std::unexpected(std::move(then_block.error()));
        }
        ExprPtr else_branch;
        if (expr.else_branch) {
            auto lowered = lower_expr(**expr.else_branch);
            if (!lowered) {
                return std::unexpected(std::move(lowered.error()));
            }
            else_branch = std::move(*lowered);
        }
        return make_expr(span, IfExpr{
                                   .cond = std::move(*cond),
                                   .then_block = std::move(*then_block),
                                   .else_branch = std::move(else_branch),
                               });
    }

    [[nodiscard]] static UnaryOperator lower_unary(UnaryOp operation) {
        return operation == UnaryOp::Neg ? UnaryOperator::Negate : UnaryOperator::Not;
    }

    [[nodiscard]] static BinaryOperator lower_binary_op(BinaryOp operation) {
        switch (operation) {
        case BinaryOp::Or:
            return BinaryOperator::Or;
        case BinaryOp::And:
            return BinaryOperator::And;
        case BinaryOp::Eq:
            return BinaryOperator::Equal;
        case BinaryOp::Ne:
            return BinaryOperator::NotEqual;
        case BinaryOp::Lt:
            return BinaryOperator::Less;
        case BinaryOp::Le:
            return BinaryOperator::LessEqual;
        case BinaryOp::Gt:
            return BinaryOperator::Greater;
        case BinaryOp::Ge:
            return BinaryOperator::GreaterEqual;
        case BinaryOp::Add:
            return BinaryOperator::Add;
        case BinaryOp::Sub:
            return BinaryOperator::Subtract;
        case BinaryOp::Mul:
            return BinaryOperator::Multiply;
        case BinaryOp::Div:
            return BinaryOperator::Divide;
        case BinaryOp::Rem:
            return BinaryOperator::Remainder;
        }
        return BinaryOperator::Add;
    }

    [[nodiscard]] Result<ExprPtr> lower_binary(const syntax::ast::Binary& expr, syntax::Span span) {
        auto lhs = lower_expr(*expr.lhs);
        if (!lhs) {
            return std::unexpected(std::move(lhs.error()));
        }
        auto rhs = lower_expr(*expr.rhs);
        if (!rhs) {
            return std::unexpected(std::move(rhs.error()));
        }
        return make_expr(span, BinaryExpr{
                                   .op = lower_binary_op(expr.op),
                                   .lhs = std::move(*lhs),
                                   .rhs = std::move(*rhs),
                               });
    }

    [[nodiscard]] Result<std::vector<CallArg>>
    lower_args(const std::vector<syntax::ast::Arg>& args) {
        std::vector<CallArg> lowered;
        lowered.reserve(args.size());
        for (const syntax::ast::Arg& arg : args) {
            if (const auto* positional = std::get_if<syntax::ast::ExprPtr>(&arg)) {
                auto value = lower_expr(**positional);
                if (!value) {
                    return std::unexpected(std::move(value.error()));
                }
                lowered.push_back(CallArg{.name = std::nullopt, .value = std::move(*value)});
            } else {
                const auto& named = std::get<syntax::ast::NamedArg>(arg);
                auto value = lower_expr(*named.value);
                if (!value) {
                    return std::unexpected(std::move(value.error()));
                }
                lowered.push_back(
                    CallArg{.name = std::string{named.name.text}, .value = std::move(*value)});
            }
        }
        return lowered;
    }

    [[nodiscard]] Result<ExprPtr> lower_call(const syntax::ast::Call& call, syntax::Span span) {
        auto callee = lower_expr(*call.callee);
        if (!callee) {
            return std::unexpected(std::move(callee.error()));
        }
        auto args = lower_args(call.args);
        if (!args) {
            return std::unexpected(std::move(args.error()));
        }
        if (call.trailing) {
            auto trailing = lower_expr(**call.trailing);
            if (!trailing) {
                return std::unexpected(std::move(trailing.error()));
            }
            args->push_back(CallArg{.name = std::nullopt, .value = std::move(*trailing)});
        }
        return make_expr(span, CallExpr{.callee = std::move(*callee), .args = std::move(*args)});
    }

    [[nodiscard]] Result<ExprPtr> lower_method(const syntax::ast::Method& method,
                                               syntax::Span span) {
        auto receiver = lower_expr(*method.receiver);
        if (!receiver) {
            return std::unexpected(std::move(receiver.error()));
        }
        auto args = lower_args(method.args);
        if (!args) {
            return std::unexpected(std::move(args.error()));
        }
        if (method.trailing) {
            auto trailing = lower_expr(**method.trailing);
            if (!trailing) {
                return std::unexpected(std::move(trailing.error()));
            }
            args->push_back(CallArg{.name = std::nullopt, .value = std::move(*trailing)});
        }
        return make_expr(span, MethodExpr{
                                   .receiver = std::move(*receiver),
                                   .name = std::string{method.name.text},
                                   .args = std::move(*args),
                               });
    }

    [[nodiscard]] Result<ExprPtr> lower_lambda(const syntax::ast::Lambda& lambda,
                                               syntax::Span span) {
        auto body = lower_expr(*lambda.body);
        if (!body) {
            return std::unexpected(std::move(body.error()));
        }
        LambdaExpr lowered;
        lowered.params.reserve(lambda.params.size());
        for (const syntax::ast::Ident& param : lambda.params) {
            lowered.params.emplace_back(param.text);
        }
        lowered.body = std::move(*body);
        return make_expr(span, std::move(lowered));
    }

    [[nodiscard]] Result<ExprPtr> lower_ask(const syntax::ast::Ask& ask, syntax::Span span) {
        auto target = lower_expr(*ask.target);
        if (!target) {
            return std::unexpected(std::move(target.error()));
        }
        auto args = lower_args(ask.args);
        if (!args) {
            return std::unexpected(std::move(args.error()));
        }
        return make_expr(span, AskExpr{.target = std::move(*target), .args = std::move(*args)});
    }

    [[nodiscard]] Result<ExprPtr> lower_fail(const syntax::ast::FailExpr& fail, syntax::Span span) {
        FailExpr lowered{.kind = std::string{fail.kind.text}, .args = {}};
        lowered.args.reserve(fail.args.size());
        for (const auto& arg : fail.args) {
            auto value = lower_expr(*arg);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            lowered.args.push_back(std::move(*value));
        }
        return make_expr(span, std::move(lowered));
    }

    [[nodiscard]] Result<ExprPtr> lower_interp(const syntax::ast::Interp& interp,
                                               syntax::Span span) {
        StringLit lowered;
        lowered.raw = is_raw_string(interp);
        lowered.parts.reserve(interp.parts.size());
        for (const syntax::ast::InterpPart& part : interp.parts) {
            if (const auto* text = std::get_if<std::string_view>(&part)) {
                lowered.parts.emplace_back(decode_string_part(*text, lowered.raw));
            } else {
                auto value = lower_expr(**std::get_if<syntax::ast::ExprPtr>(&part));
                if (!value) {
                    return std::unexpected(std::move(value.error()));
                }
                lowered.parts.emplace_back(std::move(*value));
            }
        }
        return make_expr(span, std::move(lowered));
    }

    [[nodiscard]] Result<Block> lower_block(const syntax::ast::Block& block) {
        Block lowered{.span = block.span, .statements = {}, .tail = nullptr};
        lowered.statements.reserve(block.stmts.size());
        for (const syntax::ast::Stmt& stmt : block.stmts) {
            if (const auto* let = std::get_if<syntax::ast::LetStmt>(&stmt)) {
                auto value = lower_expr(*let->value);
                if (!value) {
                    return std::unexpected(std::move(value.error()));
                }
                TypeRefPtr type;
                if (let->type) {
                    auto lowered_type = lower_type(*let->type);
                    if (!lowered_type) {
                        return std::unexpected(std::move(lowered_type.error()));
                    }
                    type = *lowered_type;
                }
                lowered.statements.push_back(Stmt{
                    .span = let->span,
                    .node =
                        LetStmt{
                            .name = std::string{let->name.text},
                            .type = std::move(type),
                            .value = std::move(*value),
                        },
                });
            } else {
                const auto& expression = std::get<syntax::ast::ExprStmt>(stmt);
                auto value = lower_expr(*expression.expr);
                if (!value) {
                    return std::unexpected(std::move(value.error()));
                }
                lowered.statements.push_back(
                    Stmt{.span = expression.span, .node = ExprStmt{.expr = std::move(*value)}});
            }
        }
        if (block.tail) {
            auto tail = lower_expr(**block.tail);
            if (!tail) {
                return std::unexpected(std::move(tail.error()));
            }
            lowered.tail = std::move(*tail);
        } else if (!lowered.statements.empty() &&
                   std::holds_alternative<ExprStmt>(lowered.statements.back().node)) {
            // WP-03's parser does not yet record `Block::tail`; the final
            // expression statement is the block's value (§4.1). WP-07 moves
            // this decision into the parser.
            lowered.tail = std::move(std::get<ExprStmt>(lowered.statements.back().node).expr);
            lowered.statements.pop_back();
        }
        return lowered;
    }

    [[nodiscard]] Result<TypeRefPtr> lower_type(const TypeExpr& type) {
        return std::visit(
            [&](const auto& node) -> Result<TypeRefPtr> {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, syntax::ast::NamedType>) {
                    const std::string name = join_path(node.path);
                    return named_type(name);
                } else if constexpr (std::is_same_v<Node, syntax::ast::ListType>) {
                    auto element = lower_type(*node.elem);
                    if (!element) {
                        return std::unexpected(std::move(element.error()));
                    }
                    auto ref = std::make_shared<TypeRef>();
                    ref->kind = TypeRef::Kind::List;
                    ref->element = *element;
                    return Result<TypeRefPtr>{ref};
                } else if constexpr (std::is_same_v<Node, syntax::ast::OptionalType>) {
                    auto inner = lower_type(*node.inner);
                    if (!inner) {
                        return std::unexpected(std::move(inner.error()));
                    }
                    auto ref = std::make_shared<TypeRef>();
                    ref->kind = TypeRef::Kind::Optional;
                    ref->element = *inner;
                    return Result<TypeRefPtr>{ref};
                } else {
                    auto ref = std::make_shared<TypeRef>();
                    ref->kind = TypeRef::Kind::Record;
                    ref->fields.reserve(node.fields.size());
                    for (const syntax::ast::RecordField& field : node.fields) {
                        auto field_type = lower_type(*field.type);
                        if (!field_type) {
                            return std::unexpected(std::move(field_type.error()));
                        }
                        ref->fields.emplace_back(std::string{field.name.text}, *field_type);
                    }
                    return Result<TypeRefPtr>{ref};
                }
            },
            type.kind);
    }

    [[nodiscard]] static Result<TypeRefPtr> named_type(const std::string& name) {
        auto ref = std::make_shared<TypeRef>();
        ref->name = name;
        if (name == "Unit") {
            ref->kind = TypeRef::Kind::Unit;
        } else if (name == "Bool") {
            ref->kind = TypeRef::Kind::Bool;
        } else if (name == "Int") {
            ref->kind = TypeRef::Kind::Int;
        } else if (name == "Float") {
            ref->kind = TypeRef::Kind::Float;
        } else if (name == "String") {
            ref->kind = TypeRef::Kind::Str;
        } else if (name == "Duration") {
            ref->kind = TypeRef::Kind::Duration;
        } else if (name == "Money") {
            ref->kind = TypeRef::Kind::Money;
        } else if (name == "Json") {
            ref->kind = TypeRef::Kind::Json;
        } else if (name.empty()) {
            ref->kind = TypeRef::Kind::Unknown;
        } else {
            ref->kind = TypeRef::Kind::Named;
        }
        return Result<TypeRefPtr>{ref};
    }

    [[nodiscard]] Result<FnDecl> lower_fn(const syntax::ast::FnDecl& declaration) {
        FnDecl function_decl;
        function_decl.name = std::string{declaration.name.text};
        function_decl.params.reserve(declaration.params.size());
        for (const syntax::ast::Param& param : declaration.params) {
            auto type = param.type ? lower_type(*param.type) : Result<TypeRefPtr>{nullptr};
            if (!type) {
                return std::unexpected(std::move(type.error()));
            }
            ExprPtr default_value;
            if (param.default_value) {
                auto value = lower_expr(*param.default_value);
                if (!value) {
                    return std::unexpected(std::move(value.error()));
                }
                default_value = std::move(*value);
            }
            function_decl.params.push_back(Param{
                .name = std::string{param.name.text},
                .type = *type,
                .default_value = std::move(default_value),
            });
        }
        if (declaration.ret) {
            auto result = lower_type(*declaration.ret);
            if (!result) {
                return std::unexpected(std::move(result.error()));
            }
            function_decl.result = *result;
        }
        auto body = lower_block(declaration.body);
        if (!body) {
            return std::unexpected(std::move(body.error()));
        }
        function_decl.body = std::move(*body);
        return function_decl;
    }

    [[nodiscard]] Result<Flow> lower_flow(const syntax::ast::FlowDecl& declaration) {
        auto type = declaration.param.type ? lower_type(*declaration.param.type)
                                           : Result<TypeRefPtr>{nullptr};
        if (!type) {
            return std::unexpected(std::move(type.error()));
        }
        Flow flow;
        flow.name = std::string{declaration.name.text};
        flow.param = Param{
            .name = std::string{declaration.param.name.text},
            .type = *type,
            .default_value = nullptr,
        };
        if (declaration.ret) {
            auto result = lower_type(*declaration.ret);
            if (!result) {
                return std::unexpected(std::move(result.error()));
            }
            flow.result = *result;
        }
        auto body = lower_block(declaration.body);
        if (!body) {
            return std::unexpected(std::move(body.error()));
        }
        flow.body = std::move(*body);
        return flow;
    }

    [[nodiscard]] Result<Task> lower_task(const syntax::ast::TaskDecl& declaration) {
        Task task{
            .name = std::string{declaration.name.text},
            .input = std::nullopt,
            .output = nullptr,
            .budget = nullptr,
        };
        for (const syntax::ast::TaskSection& section : declaration.sections) {
            if (const auto* input = std::get_if<syntax::ast::TaskInput>(&section)) {
                auto type = std::make_shared<TypeRef>();
                type->kind = TypeRef::Kind::Record;
                type->name = std::string{declaration.name.text} + ".input";
                for (const syntax::ast::RecordField& field : input->type.fields) {
                    auto field_type = lower_type(*field.type);
                    if (!field_type) {
                        return std::unexpected(std::move(field_type.error()));
                    }
                    type->fields.emplace_back(std::string{field.name.text}, *field_type);
                }
                task.input = TypeRefPtr{std::move(type)};
            } else if (const auto* output = std::get_if<syntax::ast::TaskOutput>(&section)) {
                auto type = lower_type(*output->type);
                if (!type) {
                    return std::unexpected(std::move(type.error()));
                }
                task.output = *type;
            } else {
                const auto& budget = std::get<syntax::ast::TaskBudget>(section);
                auto value = lower_expr(*budget.value);
                if (!value) {
                    return std::unexpected(std::move(value.error()));
                }
                task.budget = std::move(*value);
            }
        }
        return task;
    }

    std::string module_;
};

} // namespace

bool same_type(const TypeRef& lhs, const TypeRef& rhs) {
    if (lhs.kind != rhs.kind || lhs.name != rhs.name) {
        return false;
    }
    if (static_cast<bool>(lhs.element) != static_cast<bool>(rhs.element)) {
        return false;
    }
    if (lhs.element && !same_type(*lhs.element, *rhs.element)) {
        return false;
    }
    if (lhs.fields.size() != rhs.fields.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.fields.size(); ++index) {
        if (lhs.fields.at(index).first != rhs.fields.at(index).first ||
            !same_type(*lhs.fields.at(index).second, *rhs.fields.at(index).second)) {
            return false;
        }
    }
    return true;
}

std::string type_name(const TypeRef& type) {
    switch (type.kind) {
    case TypeRef::Kind::Unknown:
        return "Any";
    case TypeRef::Kind::Unit:
        return "Unit";
    case TypeRef::Kind::Bool:
        return "Bool";
    case TypeRef::Kind::Int:
        return "Int";
    case TypeRef::Kind::Float:
        return "Float";
    case TypeRef::Kind::Str:
        return "String";
    case TypeRef::Kind::Duration:
        return "Duration";
    case TypeRef::Kind::Money:
        return "Money";
    case TypeRef::Kind::Json:
        return "Json";
    case TypeRef::Kind::Named:
        return type.name;
    case TypeRef::Kind::List:
        return "[" + (type.element ? type_name(*type.element) : "Any") + "]";
    case TypeRef::Kind::Optional:
        return (type.element ? type_name(*type.element) : "Any") + "?";
    case TypeRef::Kind::Record: {
        std::string out = type.name.empty() ? "{" : type.name + " {";
        for (std::size_t index = 0; index < type.fields.size(); ++index) {
            if (index != 0) {
                out += ", ";
            }
            out +=
                type.fields.at(index).first + ": " +
                (type.fields.at(index).second ? type_name(*type.fields.at(index).second) : "Any");
        }
        return out + "}";
    }
    }
    return "Any";
}

const FnDecl* Program::find_fn(std::string_view name) const noexcept {
    const auto found = std::ranges::find_if(
        fns, [&](const FnDecl& declaration) { return declaration.name == name; });
    return found == fns.end() ? nullptr : &*found;
}

const Flow* Program::find_flow(std::string_view name) const noexcept {
    const auto found =
        std::ranges::find_if(flows, [&](const Flow& flow) { return flow.name == name; });
    return found == flows.end() ? nullptr : &*found;
}

const Const* Program::find_const(std::string_view name) const noexcept {
    const auto found =
        std::ranges::find_if(consts, [&](const Const& constant) { return constant.name == name; });
    return found == consts.end() ? nullptr : &*found;
}

const Agent* Program::find_agent(std::string_view name) const noexcept {
    const auto found =
        std::ranges::find_if(agents, [&](const Agent& agent) { return agent.name == name; });
    return found == agents.end() ? nullptr : &*found;
}

const Task* Program::find_task(std::string_view name) const noexcept {
    const auto found =
        std::ranges::find_if(tasks, [&](const Task& task) { return task.name == name; });
    return found == tasks.end() ? nullptr : &*found;
}

std::string_view expr_kind_name(const Expr& expr) {
    return std::visit(
        [](const auto& node) -> std::string_view {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, UnitLit>) {
                return "unit";
            } else if constexpr (std::is_same_v<Node, NullLit>) {
                return "null";
            } else if constexpr (std::is_same_v<Node, BoolLit>) {
                return "bool";
            } else if constexpr (std::is_same_v<Node, IntLit>) {
                return "int";
            } else if constexpr (std::is_same_v<Node, FloatLit>) {
                return "float";
            } else if constexpr (std::is_same_v<Node, DurationLit>) {
                return "duration";
            } else if constexpr (std::is_same_v<Node, StringLit>) {
                return "string";
            } else if constexpr (std::is_same_v<Node, ListExpr>) {
                return "list";
            } else if constexpr (std::is_same_v<Node, RecordExpr>) {
                return "record";
            } else if constexpr (std::is_same_v<Node, EnumLit>) {
                return "enum";
            } else if constexpr (std::is_same_v<Node, NameExpr>) {
                return "name";
            } else if constexpr (std::is_same_v<Node, IfExpr>) {
                return "if";
            } else if constexpr (std::is_same_v<Node, Block>) {
                return "block";
            } else if constexpr (std::is_same_v<Node, UnaryExpr>) {
                return "unary";
            } else if constexpr (std::is_same_v<Node, BinaryExpr>) {
                return "binary";
            } else if constexpr (std::is_same_v<Node, CallExpr>) {
                return "call";
            } else if constexpr (std::is_same_v<Node, MethodExpr>) {
                return "method";
            } else if constexpr (std::is_same_v<Node, FieldExpr>) {
                return "field";
            } else if constexpr (std::is_same_v<Node, LambdaExpr>) {
                return "lambda";
            } else if constexpr (std::is_same_v<Node, AskExpr>) {
                return "ask";
            } else if constexpr (std::is_same_v<Node, FailExpr>) {
                return "fail";
            } else {
                return "complete";
            }
        },
        expr.node);
}

std::string_view stmt_kind_name(const Stmt& stmt) noexcept {
    return std::holds_alternative<LetStmt>(stmt.node) ? "let" : "expr";
}

std::expected<Program, Failure> from_ast(const syntax::ast::Program& ast) {
    std::string module;
    if (ast.module_path) {
        module = join_path(*ast.module_path);
    }
    return Lowerer{std::move(module)}.lower(ast);
}

} // namespace leash::runtime
