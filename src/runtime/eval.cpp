// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/runtime/eval.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace leash::runtime {
namespace {

using EvalResult = std::expected<Value, Control>;
using EvaluatedArgs = std::pair<std::vector<Value>, std::map<std::string, Value, std::less<>>>;

[[nodiscard]] Failure runtime_failure(std::string message, const Program& program,
                                      syntax::Span span, std::string_view node_kind,
                                      spec::FailureKind kind = spec::FailureKind::Unknown) {
    static_cast<void>(span);
    return Failure{
        .kind = kind,
        .message = std::move(message),
        .data = std::nullopt,
        .site =
            Site{
                .module = program.module,
                .item = {},
                .node_kind = std::string{node_kind},
                .line = 0,
            },
        .cause = nullptr,
    };
}

[[nodiscard]] std::expected<Value, Control> fail_result(Failure failure) {
    return std::unexpected(Control{Fail{std::move(failure)}});
}

[[nodiscard]] bool is_finite(double value) { return std::isfinite(value); }

[[nodiscard]] std::optional<std::int64_t> checked_add(std::int64_t lhs, std::int64_t rhs) {
    if ((rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() - rhs) ||
        (rhs < 0 && lhs < std::numeric_limits<std::int64_t>::min() - rhs)) {
        return std::nullopt;
    }
    return lhs + rhs;
}

[[nodiscard]] std::optional<std::int64_t> checked_sub(std::int64_t lhs, std::int64_t rhs) {
    if ((rhs < 0 && lhs > std::numeric_limits<std::int64_t>::max() + rhs) ||
        (rhs > 0 && lhs < std::numeric_limits<std::int64_t>::min() + rhs)) {
        return std::nullopt;
    }
    return lhs - rhs;
}

[[nodiscard]] std::optional<std::int64_t> checked_mul(std::int64_t lhs, std::int64_t rhs) {
    std::int64_t result = 0;
    if (__builtin_mul_overflow(lhs, rhs, &result)) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<std::int64_t> checked_negate(std::int64_t value) {
    if (value == std::numeric_limits<std::int64_t>::min()) {
        return std::nullopt;
    }
    return -value;
}

[[nodiscard]] const Value* field_of(const Value& value, std::string_view name) {
    const auto* record = std::get_if<Record>(&value);
    if (record == nullptr || !record->v) {
        return nullptr;
    }
    const auto found = record->v->fields.find(name);
    return found == record->v->fields.end() ? nullptr : &found->second;
}

[[nodiscard]] int ordering_of(bool less, bool greater) {
    if (less) {
        return -1;
    }
    if (greater) {
        return 1;
    }
    return 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
[[nodiscard]] std::optional<int> compare_values(const Value& lhs, const Value& rhs) {
    if (const auto* left = std::get_if<Int>(&lhs)) {
        if (const auto* right = std::get_if<Int>(&rhs)) {
            return ordering_of(left->v<right->v, left->v> right->v);
        }
    }
    if (const auto* left = std::get_if<Float>(&lhs)) {
        if (const auto* right = std::get_if<Float>(&rhs)) {
            return ordering_of(left->v<right->v, left->v> right->v);
        }
    }
    if (const auto* left = std::get_if<Str>(&lhs)) {
        if (const auto* right = std::get_if<Str>(&rhs)) {
            const std::string empty;
            const std::string& left_text = left->v ? *left->v : empty;
            const std::string& right_text = right->v ? *right->v : empty;
            return ordering_of(left_text<right_text, left_text> right_text);
        }
    }
    if (const auto* left = std::get_if<std::chrono::nanoseconds>(&lhs)) {
        if (const auto* right = std::get_if<std::chrono::nanoseconds>(&rhs)) {
            return ordering_of(*left<*right, *left> * right);
        }
    }
    if (const auto* left = std::get_if<spec::Money>(&lhs)) {
        if (const auto* right = std::get_if<spec::Money>(&rhs)) {
            return ordering_of(left->micros_value() < right->micros_value(),
                               left->micros_value() > right->micros_value());
        }
    }
    return std::nullopt;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
[[nodiscard]] std::expected<Value, Failure> apply_arithmetic(BinaryOperator operation,
                                                             const Value& lhs, const Value& rhs,
                                                             const Program& program,
                                                             syntax::Span span) {
    const auto mismatch = [&] -> std::expected<Value, Failure> {
        return std::unexpected(runtime_failure("operator is not defined for " +
                                                   std::string{kind_name(lhs)} + " and " +
                                                   std::string{kind_name(rhs)},
                                               program, span, "binary"));
    };

    if (const auto* left = std::get_if<Int>(&lhs)) {
        if (const auto* right = std::get_if<Int>(&rhs)) {
            std::optional<std::int64_t> result;
            switch (operation) {
            case BinaryOperator::Add:
                result = checked_add(left->v, right->v);
                break;
            case BinaryOperator::Subtract:
                result = checked_sub(left->v, right->v);
                break;
            case BinaryOperator::Multiply:
                result = checked_mul(left->v, right->v);
                break;
            case BinaryOperator::Divide:
                if (right->v == 0) {
                    return std::unexpected(
                        runtime_failure("division by zero", program, span, "binary"));
                }
                if (left->v == std::numeric_limits<std::int64_t>::min() && right->v == -1) {
                    return std::unexpected(
                        runtime_failure("integer overflow", program, span, "binary"));
                }
                result = left->v / right->v;
                break;
            case BinaryOperator::Remainder:
                if (right->v == 0) {
                    return std::unexpected(
                        runtime_failure("division by zero", program, span, "binary"));
                }
                result = left->v % right->v;
                break;
            case BinaryOperator::Or:
            case BinaryOperator::And:
            case BinaryOperator::Equal:
            case BinaryOperator::NotEqual:
            case BinaryOperator::Less:
            case BinaryOperator::LessEqual:
            case BinaryOperator::Greater:
            case BinaryOperator::GreaterEqual:
                return mismatch();
            }
            if (!result) {
                return std::unexpected(
                    runtime_failure("integer overflow", program, span, "binary"));
            }
            return integer(*result);
        }
    }

    if (const auto* left = std::get_if<Float>(&lhs)) {
        if (const auto* right = std::get_if<Float>(&rhs)) {
            double result = 0.0;
            switch (operation) {
            case BinaryOperator::Add:
                result = left->v + right->v;
                break;
            case BinaryOperator::Subtract:
                result = left->v - right->v;
                break;
            case BinaryOperator::Multiply:
                result = left->v * right->v;
                break;
            case BinaryOperator::Divide:
                if (right->v == 0.0) {
                    return std::unexpected(
                        runtime_failure("division by zero", program, span, "binary"));
                }
                result = left->v / right->v;
                break;
            case BinaryOperator::Remainder:
                if (right->v == 0.0) {
                    return std::unexpected(
                        runtime_failure("division by zero", program, span, "binary"));
                }
                result = std::fmod(left->v, right->v);
                break;
            case BinaryOperator::Or:
            case BinaryOperator::And:
            case BinaryOperator::Equal:
            case BinaryOperator::NotEqual:
            case BinaryOperator::Less:
            case BinaryOperator::LessEqual:
            case BinaryOperator::Greater:
            case BinaryOperator::GreaterEqual:
                return mismatch();
            }
            if (!is_finite(result)) {
                return std::unexpected(runtime_failure("float overflow", program, span, "binary"));
            }
            return number(result);
        }
    }

    if (const auto* left = std::get_if<Str>(&lhs)) {
        if (const auto* right = std::get_if<Str>(&rhs)) {
            if (operation != BinaryOperator::Add) {
                return mismatch();
            }
            const std::string empty;
            return string((left->v ? *left->v : empty) + (right->v ? *right->v : empty));
        }
    }

    if (const auto* left = std::get_if<List>(&lhs)) {
        if (const auto* right = std::get_if<List>(&rhs)) {
            if (operation != BinaryOperator::Add) {
                return mismatch();
            }
            std::vector<Value> combined;
            if (left->v) {
                combined.insert(combined.end(), left->v->items.begin(), left->v->items.end());
            }
            if (right->v) {
                combined.insert(combined.end(), right->v->items.begin(), right->v->items.end());
            }
            return list(std::move(combined));
        }
    }

    if (const auto* left = std::get_if<std::chrono::nanoseconds>(&lhs)) {
        if (const auto* right = std::get_if<std::chrono::nanoseconds>(&rhs)) {
            if (operation == BinaryOperator::Add) {
                const auto result = checked_add(left->count(), right->count());
                if (!result) {
                    return std::unexpected(
                        runtime_failure("duration overflow", program, span, "binary"));
                }
                return duration(std::chrono::nanoseconds{*result});
            }
            if (operation == BinaryOperator::Subtract) {
                const auto result = checked_sub(left->count(), right->count());
                if (!result) {
                    return std::unexpected(
                        runtime_failure("duration overflow", program, span, "binary"));
                }
                return duration(std::chrono::nanoseconds{*result});
            }
            return mismatch();
        }
        if (const auto* factor = std::get_if<Int>(&rhs)) {
            if (operation == BinaryOperator::Multiply) {
                const auto result = checked_mul(left->count(), factor->v);
                if (!result) {
                    return std::unexpected(
                        runtime_failure("duration overflow", program, span, "binary"));
                }
                return duration(std::chrono::nanoseconds{*result});
            }
            if (operation == BinaryOperator::Divide) {
                if (factor->v == 0) {
                    return std::unexpected(
                        runtime_failure("division by zero", program, span, "binary"));
                }
                return duration(std::chrono::nanoseconds{left->count() / factor->v});
            }
            return mismatch();
        }
    }

    if (const auto* left = std::get_if<spec::Money>(&lhs)) {
        if (const auto* right = std::get_if<spec::Money>(&rhs)) {
            if (operation == BinaryOperator::Add) {
                const auto result = checked_add(left->micros_value(), right->micros_value());
                if (!result) {
                    return std::unexpected(
                        runtime_failure("money overflow", program, span, "binary"));
                }
                return money(spec::Money::micros(*result));
            }
            if (operation == BinaryOperator::Subtract) {
                const auto result = left->checked_sub(*right);
                if (!result) {
                    return std::unexpected(
                        runtime_failure("money underflow", program, span, "binary"));
                }
                return money(*result);
            }
            return mismatch();
        }
    }

    return mismatch();
}

[[nodiscard]] std::expected<Value, Failure> apply_unary(UnaryOperator operation,
                                                        const Value& operand,
                                                        const Program& program, syntax::Span span) {
    if (operation == UnaryOperator::Not) {
        if (const auto* boolean_value = std::get_if<Bool>(&operand)) {
            return boolean(!boolean_value->v);
        }
        return std::unexpected(
            runtime_failure("'not' expects Bool, found " + std::string{kind_name(operand)}, program,
                            span, "unary"));
    }
    if (const auto* integer_value = std::get_if<Int>(&operand)) {
        const auto result = checked_negate(integer_value->v);
        if (!result) {
            return std::unexpected(runtime_failure("integer overflow", program, span, "unary"));
        }
        return integer(*result);
    }
    if (const auto* number_value = std::get_if<Float>(&operand)) {
        return number(-number_value->v);
    }
    if (const auto* duration_value = std::get_if<std::chrono::nanoseconds>(&operand)) {
        if (duration_value->count() == std::numeric_limits<std::int64_t>::min()) {
            return std::unexpected(runtime_failure("duration overflow", program, span, "unary"));
        }
        return duration(std::chrono::nanoseconds{-duration_value->count()});
    }
    if (const auto* money_value = std::get_if<spec::Money>(&operand)) {
        if (money_value->micros_value() == std::numeric_limits<std::int64_t>::min()) {
            return std::unexpected(runtime_failure("money overflow", program, span, "unary"));
        }
        return money(spec::Money::micros(-money_value->micros_value()));
    }
    return std::unexpected(runtime_failure(
        "unary '-' is not defined for " + std::string{kind_name(operand)}, program, span, "unary"));
}

} // namespace

Interpreter::Interpreter(const Program& program) : program_(program) {
    root_env_ = Env{};
    const auto define = [this](const std::string& name, std::vector<std::string> params) {
        root_env_ = root_env_.define(name, make_builtin(name, std::move(params)));
    };
    define("len", {"value"});
    define("usd", {"amount"});
    define("to_string", {"value"});
    define("filter", {"items", "predicate"});
    define("map", {"items", "transform"});
    define("any", {"items", "predicate"});
    define("all", {"items", "predicate"});
    define("first", {"items"});
    define("contains", {"haystack", "needle"});
    define("lines", {"text"});
    define("trim", {"text"});
}

Value Interpreter::make_builtin(const std::string& name, std::vector<std::string> params) {
    auto closure = std::make_shared<Closure>();
    closure->name = name;
    closure->params = std::move(params);
    closure->defaults.resize(closure->params.size(), nullptr);
    closure->builtin = name;
    return function(std::move(closure));
}

Closure Interpreter::make_fn_closure(const FnDecl& function_decl) const {
    Closure closure;
    closure.name = function_decl.name;
    closure.params.reserve(function_decl.params.size());
    closure.defaults.reserve(function_decl.params.size());
    for (const Param& param : function_decl.params) {
        closure.params.push_back(param.name);
        closure.defaults.push_back(param.default_value.get());
    }
    closure.block = &function_decl.body;
    closure.env = root_env_;
    return closure;
}

Closure Interpreter::make_flow_closure(const Flow& flow) const {
    Closure closure;
    closure.name = flow.name;
    closure.params = {flow.param.name};
    closure.defaults = {nullptr};
    closure.block = &flow.body;
    closure.env = root_env_;
    return closure;
}

std::optional<Value> Interpreter::lookup_callable(std::string_view name, const Env& env) const {
    if (auto value = env.lookup(name)) {
        return std::optional<Value>{std::move(*value)};
    }
    if (const FnDecl* function_decl = program_.find_fn(name)) {
        return function(std::make_shared<const Closure>(make_fn_closure(*function_decl)));
    }
    if (const Flow* flow = program_.find_flow(name)) {
        return function(std::make_shared<const Closure>(make_flow_closure(*flow)));
    }
    return std::nullopt;
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
asio::awaitable<EvalResult> Interpreter::eval(const Expr& expr) {
    co_return co_await eval_impl(&expr, root_env_);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
asio::awaitable<EvalResult> Interpreter::eval_impl(const Expr* expr, Env env) {
    const syntax::Span span = expr->span;
    // Exhaustive variant dispatch; the coroutine copies its captures by value.
    // NOLINTNEXTLINE
    const auto run = [this, span, env](const auto& node) -> asio::awaitable<EvalResult> {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node, UnitLit>) {
            co_return unit();
        } else if constexpr (std::is_same_v<Node, NullLit>) {
            co_return null();
        } else if constexpr (std::is_same_v<Node, BoolLit>) {
            co_return boolean(node.value);
        } else if constexpr (std::is_same_v<Node, IntLit>) {
            co_return integer(node.value);
        } else if constexpr (std::is_same_v<Node, FloatLit>) {
            co_return number(node.value);
        } else if constexpr (std::is_same_v<Node, DurationLit>) {
            co_return duration(node.value);
        } else if constexpr (std::is_same_v<Node, StringLit>) {
            co_return co_await eval_string(&node, env);
        } else if constexpr (std::is_same_v<Node, ListExpr>) {
            std::vector<Value> elements;
            elements.reserve(node.elements.size());
            for (const ExprPtr& element : node.elements) {
                auto value = co_await eval_impl(element.get(), env);
                if (!value) {
                    co_return std::unexpected(std::move(value).error());
                }
                elements.push_back(std::move(*value));
            }
            co_return list(std::move(elements));
        } else if constexpr (std::is_same_v<Node, RecordExpr>) {
            std::map<std::string, Value, std::less<>> fields;
            for (const RecordFieldValue& field : node.fields) {
                auto value = co_await eval_impl(field.value.get(), env);
                if (!value) {
                    co_return std::unexpected(std::move(value).error());
                }
                fields.insert_or_assign(field.name, std::move(*value));
            }
            co_return record(node.type, std::move(fields));
        } else if constexpr (std::is_same_v<Node, EnumLit>) {
            std::vector<Value> payload;
            payload.reserve(node.payload.size());
            for (const ExprPtr& element : node.payload) {
                auto value = co_await eval_impl(element.get(), env);
                if (!value) {
                    co_return std::unexpected(std::move(value).error());
                }
                payload.push_back(std::move(*value));
            }
            co_return enum_value(node.type, node.variant, std::move(payload));
        } else if constexpr (std::is_same_v<Node, NameExpr>) {
            co_return co_await eval_name(&node, env);
        } else if constexpr (std::is_same_v<Node, IfExpr>) {
            auto condition = co_await eval_impl(node.cond.get(), env);
            if (!condition) {
                co_return std::unexpected(std::move(condition).error());
            }
            const auto* boolean = std::get_if<Bool>(&*condition);
            if (boolean == nullptr) {
                co_return fail_result(runtime_failure("'if' condition must be Bool, found " +
                                                          std::string{kind_name(*condition)},
                                                      program_, span, "if"));
            }
            if (boolean->v) {
                co_return co_await eval_block(&node.then_block, env);
            }
            if (node.else_branch) {
                co_return co_await eval_impl(node.else_branch.get(), env);
            }
            co_return unit();
        } else if constexpr (std::is_same_v<Node, Block>) {
            co_return co_await eval_block(&node, env);
        } else if constexpr (std::is_same_v<Node, UnaryExpr>) {
            auto operand = co_await eval_impl(node.operand.get(), env);
            if (!operand) {
                co_return std::unexpected(std::move(operand).error());
            }
            auto result = apply_unary(node.op, *operand, program_, span);
            if (!result) {
                co_return fail_result(std::move(result.error()));
            }
            co_return std::move(*result);
        } else if constexpr (std::is_same_v<Node, BinaryExpr>) {
            if (node.op == BinaryOperator::Or || node.op == BinaryOperator::And) {
                auto lhs = co_await eval_impl(node.lhs.get(), env);
                if (!lhs) {
                    co_return std::unexpected(std::move(lhs).error());
                }
                const auto* left = std::get_if<Bool>(&*lhs);
                if (left == nullptr) {
                    co_return fail_result(runtime_failure("logical operator expects Bool, found " +
                                                              std::string{kind_name(*lhs)},
                                                          program_, span, "binary"));
                }
                if (node.op == BinaryOperator::Or && left->v) {
                    co_return boolean(true);
                }
                if (node.op == BinaryOperator::And && !left->v) {
                    co_return boolean(false);
                }
                auto rhs = co_await eval_impl(node.rhs.get(), env);
                if (!rhs) {
                    co_return std::unexpected(std::move(rhs).error());
                }
                const auto* right = std::get_if<Bool>(&*rhs);
                if (right == nullptr) {
                    co_return fail_result(runtime_failure("logical operator expects Bool, found " +
                                                              std::string{kind_name(*rhs)},
                                                          program_, span, "binary"));
                }
                co_return boolean(right->v);
            }

            auto lhs = co_await eval_impl(node.lhs.get(), env);
            if (!lhs) {
                co_return std::unexpected(std::move(lhs).error());
            }
            auto rhs = co_await eval_impl(node.rhs.get(), env);
            if (!rhs) {
                co_return std::unexpected(std::move(rhs).error());
            }
            switch (node.op) {
            case BinaryOperator::Equal:
                co_return boolean(structurally_equal(*lhs, *rhs));
            case BinaryOperator::NotEqual:
                co_return boolean(!structurally_equal(*lhs, *rhs));
            case BinaryOperator::Less:
            case BinaryOperator::LessEqual:
            case BinaryOperator::Greater:
            case BinaryOperator::GreaterEqual: {
                const auto ordering = compare_values(*lhs, *rhs);
                if (!ordering) {
                    co_return fail_result(
                        runtime_failure("values of type " + std::string{kind_name(*lhs)} + " and " +
                                            std::string{kind_name(*rhs)} + " are not ordered",
                                        program_, span, "binary"));
                }
                bool answer = false;
                switch (node.op) {
                case BinaryOperator::Less:
                    answer = *ordering < 0;
                    break;
                case BinaryOperator::LessEqual:
                    answer = *ordering <= 0;
                    break;
                case BinaryOperator::Greater:
                    answer = *ordering > 0;
                    break;
                case BinaryOperator::GreaterEqual:
                    answer = *ordering >= 0;
                    break;
                case BinaryOperator::Or:
                case BinaryOperator::And:
                case BinaryOperator::Equal:
                case BinaryOperator::NotEqual:
                case BinaryOperator::Add:
                case BinaryOperator::Subtract:
                case BinaryOperator::Multiply:
                case BinaryOperator::Divide:
                case BinaryOperator::Remainder:
                    break;
                }
                co_return boolean(answer);
            }
            case BinaryOperator::Or:
            case BinaryOperator::And:
                co_return fail_result(runtime_failure(
                    "internal: logical operator reached arithmetic", program_, span, "binary"));
            case BinaryOperator::Add:
            case BinaryOperator::Subtract:
            case BinaryOperator::Multiply:
            case BinaryOperator::Divide:
            case BinaryOperator::Remainder: {
                auto result = apply_arithmetic(node.op, *lhs, *rhs, program_, span);
                if (!result) {
                    co_return fail_result(std::move(result.error()));
                }
                co_return std::move(*result);
            }
            }
        } else if constexpr (std::is_same_v<Node, CallExpr>) {
            co_return co_await eval_call(&node, env);
        } else if constexpr (std::is_same_v<Node, MethodExpr>) {
            co_return co_await eval_method(&node, env);
        } else if constexpr (std::is_same_v<Node, FieldExpr>) {
            auto receiver = co_await eval_impl(node.receiver.get(), env);
            if (!receiver) {
                co_return std::unexpected(std::move(receiver).error());
            }
            if (std::holds_alternative<Null>(*receiver) && node.optional) {
                co_return null();
            }
            const Value* field = field_of(*receiver, node.name);
            if (field == nullptr) {
                if (node.optional) {
                    co_return null();
                }
                co_return fail_result(runtime_failure("value of type " +
                                                          std::string{kind_name(*receiver)} +
                                                          " has no field '" + node.name + "'",
                                                      program_, span, "field"));
            }
            co_return *field;
        } else if constexpr (std::is_same_v<Node, LambdaExpr>) {
            auto closure = std::make_shared<Closure>();
            closure->name = "<lambda>";
            closure->params = node.params;
            closure->defaults.resize(node.params.size(), nullptr);
            closure->expr = node.body.get();
            closure->env = env;
            co_return function(std::move(closure));
        } else if constexpr (std::is_same_v<Node, AskExpr>) {
            co_return fail_result(
                runtime_failure("ask is not available without the effect gateway (WP-04 stage 2)",
                                program_, span, "ask"));
        } else if constexpr (std::is_same_v<Node, FailExpr>) {
            const spec::FailureKind kind = spec::failure_kind_from_string(node.kind);
            if (spec::to_string(kind) != node.kind) {
                co_return fail_result(runtime_failure("unknown failure kind '" + node.kind + "'",
                                                      program_, span, "fail"));
            }
            if (node.args.empty()) {
                co_return fail_result(
                    runtime_failure("fail needs a message", program_, span, "fail"));
            }
            auto message = co_await eval_impl(node.args.front().get(), env);
            if (!message) {
                co_return std::unexpected(std::move(message).error());
            }
            const auto* text = std::get_if<Str>(&*message);
            if (text == nullptr) {
                co_return fail_result(runtime_failure("fail message must be String, found " +
                                                          std::string{kind_name(*message)},
                                                      program_, span, "fail"));
            }
            Failure failure =
                runtime_failure(text->v ? *text->v : std::string{}, program_, span, "fail", kind);
            if (node.args.size() > 1) {
                auto data = co_await eval_impl(node.args.at(1).get(), env);
                if (!data) {
                    co_return std::unexpected(std::move(data).error());
                }
                try {
                    failure.data = to_json(*data);
                } catch (const std::invalid_argument&) {
                    failure.data = std::nullopt;
                }
            }
            co_return fail_result(std::move(failure));
        } else {
            auto value = co_await eval_impl(node.value.get(), env);
            if (!value) {
                co_return std::unexpected(std::move(value).error());
            }
            co_return std::unexpected(Control{Complete{.value = std::move(*value)}});
        }
    };
    co_return co_await std::visit(run, expr->node);
}

asio::awaitable<EvalResult> Interpreter::eval_block(const Block* block, Env env) {
    Env scope = env.child();
    for (const Stmt& stmt : block->statements) {
        if (const auto* let = std::get_if<LetStmt>(&stmt.node)) {
            auto result = co_await eval_impl(let->value.get(), scope);
            if (!result) {
                co_return std::unexpected(std::move(result).error());
            }
            scope = scope.define(let->name, std::move(*result));
            continue;
        }
        const auto& expression = std::get<ExprStmt>(stmt.node);
        auto result = co_await eval_impl(expression.expr.get(), scope);
        if (!result) {
            co_return std::unexpected(std::move(result).error());
        }
    }
    if (block->tail) {
        co_return co_await eval_impl(block->tail.get(), scope);
    }
    co_return unit();
}

asio::awaitable<EvalResult> Interpreter::eval_string(const StringLit* lit, Env env) {
    std::string out;
    for (const StringPart& part : lit->parts) {
        if (const auto* text = std::get_if<std::string>(&part)) {
            out += *text;
            continue;
        }
        auto value = co_await eval_impl(std::get_if<ExprPtr>(&part)->get(), env);
        if (!value) {
            co_return std::unexpected(std::move(value).error());
        }
        out += display(*value);
    }
    co_return string(std::move(out));
}

asio::awaitable<EvalResult> Interpreter::eval_name(const NameExpr* name, Env env) {
    std::string joined;
    for (const std::string& segment : name->path) {
        if (!joined.empty()) {
            joined.push_back('.');
        }
        joined += segment;
    }
    if (const auto value = env.lookup(joined)) {
        co_return *value;
    }
    if (const Const* constant = program_.find_const(joined)) {
        co_return co_await eval_impl(constant->value.get(), root_env_);
    }
    if (const auto value = lookup_callable(joined, env)) {
        co_return *value;
    }
    if (const Agent* agent = program_.find_agent(joined)) {
        co_return fail_result(runtime_failure("agent '" + agent->name +
                                                  "' requires the effect gateway (WP-04 stage 2)",
                                              program_, syntax::Span(), "name"));
    }
    if (name->path.size() > 1) {
        if (const auto root = env.lookup(name->path.front())) {
            const Value* current = &*root;
            bool resolved = true;
            for (std::size_t index = 1; index < name->path.size(); ++index) {
                const Value* field = field_of(*current, name->path.at(index));
                if (field == nullptr) {
                    resolved = false;
                    break;
                }
                current = field;
            }
            if (resolved) {
                co_return *current;
            }
        }
    }
    co_return fail_result(
        runtime_failure("undefined name '" + joined + "'", program_, syntax::Span(), "name"));
}

asio::awaitable<std::expected<EvaluatedArgs, Control>>
Interpreter::eval_args(const std::vector<CallArg>* args, Env env) {
    EvaluatedArgs evaluated;
    for (const CallArg& arg : *args) {
        auto value = co_await eval_impl(arg.value.get(), env);
        if (!value) {
            co_return std::unexpected(std::move(value).error());
        }
        if (arg.name) {
            evaluated.second.insert_or_assign(*arg.name, std::move(*value));
        } else {
            evaluated.first.push_back(std::move(*value));
        }
    }
    co_return evaluated;
}

asio::awaitable<EvalResult> Interpreter::eval_call(const CallExpr* call, Env env) {
    auto callee = co_await eval_impl(call->callee.get(), env);
    if (!callee) {
        co_return std::unexpected(std::move(callee).error());
    }
    auto args = co_await eval_args(&call->args, env);
    if (!args) {
        co_return std::unexpected(std::move(args).error());
    }
    co_return co_await this->call(std::move(*callee), std::move(args->first),
                                  std::move(args->second));
}

asio::awaitable<EvalResult> Interpreter::eval_method(const MethodExpr* method, Env env) {
    auto receiver = co_await eval_impl(method->receiver.get(), env);
    if (!receiver) {
        co_return std::unexpected(std::move(receiver).error());
    }
    const auto callee = lookup_callable(method->name, env);
    if (!callee) {
        co_return fail_result(runtime_failure("no method '" + method->name + "' for " +
                                                  std::string{kind_name(*receiver)},
                                              program_, method->receiver->span, "method"));
    }
    auto args = co_await eval_args(&method->args, env);
    if (!args) {
        co_return std::unexpected(std::move(args).error());
    }
    Value receiver_value = std::move(*receiver);
    args->first.insert(args->first.begin(), std::move(receiver_value));
    co_return co_await this->call(*callee, std::move(args->first), std::move(args->second));
}

asio::awaitable<EvalResult> Interpreter::call(Value calleeValue, std::vector<Value> positional,
                                              std::map<std::string, Value, std::less<>> named) {
    const auto* function_value = std::get_if<Fn>(&calleeValue);
    if (function_value == nullptr || !function_value->v) {
        co_return fail_result(runtime_failure(
            "value of type " + std::string{kind_name(calleeValue)} + " is not callable", program_,
            syntax::Span(), "call"));
    }
    co_return co_await call_closure(function_value->v, std::move(positional), std::move(named),
                                    syntax::Span());
}

asio::awaitable<EvalResult>
Interpreter::call_closure(std::shared_ptr<const Closure> closure, std::vector<Value> positional,
                          std::map<std::string, Value, std::less<>> named, syntax::Span span) {
    if (!closure->builtin.empty()) {
        co_return co_await call_builtin(closure, std::move(positional), std::move(named), span);
    }

    Env scope = closure->env.child();
    std::size_t positional_index = 0;
    for (std::size_t index = 0; index < closure->params.size(); ++index) {
        const std::string& param = closure->params.at(index);
        const auto named_value = named.find(param);
        if (positional_index < positional.size()) {
            if (named_value != named.end()) {
                co_return fail_result(
                    runtime_failure("argument '" + param + "' passed both positionally and by name",
                                    program_, span, "call"));
            }
            scope = scope.define(param, std::move(positional.at(positional_index)));
            ++positional_index;
            continue;
        }
        if (named_value != named.end()) {
            scope = scope.define(param, named_value->second);
            named.erase(named_value);
            continue;
        }
        const Expr* default_value =
            index < closure->defaults.size() ? closure->defaults.at(index) : nullptr;
        if (default_value == nullptr) {
            co_return fail_result(
                runtime_failure("missing argument '" + param + "'", program_, span, "call"));
        }
        auto value = co_await eval_impl(default_value, scope);
        if (!value) {
            co_return std::unexpected(std::move(value).error());
        }
        scope = scope.define(param, std::move(*value));
    }
    if (positional_index < positional.size() || !named.empty()) {
        co_return fail_result(runtime_failure("too many arguments to '" + closure->name + "'",
                                              program_, span, "call"));
    }

    if (closure->block != nullptr) {
        co_return co_await eval_block(closure->block, scope);
    }
    if (closure->expr != nullptr) {
        co_return co_await eval_impl(closure->expr, scope);
    }
    co_return unit();
}

asio::awaitable<EvalResult>
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
Interpreter::call_builtin(std::shared_ptr<const Closure> closure, std::vector<Value> positional,
                          std::map<std::string, Value, std::less<>> named, syntax::Span span) {
    std::vector<Value> args = std::move(positional);
    for (std::size_t index = args.size(); index < closure->params.size(); ++index) {
        const auto found = named.find(closure->params.at(index));
        if (found == named.end()) {
            co_return fail_result(runtime_failure(
                "missing argument '" + closure->params.at(index) + "'", program_, span, "call"));
        }
        args.push_back(found->second);
    }
    if (args.size() != closure->params.size()) {
        co_return fail_result(runtime_failure(
            "wrong number of arguments to '" + closure->builtin + "'", program_, span, "call"));
    }

    const auto& builtin = closure->builtin;
    if (builtin == "len") {
        if (const auto* text = std::get_if<Str>(&args.at(0))) {
            co_return integer(static_cast<std::int64_t>(text->v ? text->v->size() : 0));
        }
        if (const auto* items = std::get_if<List>(&args.at(0))) {
            co_return integer(static_cast<std::int64_t>(items->v ? items->v->items.size() : 0));
        }
        if (const auto* record_value = std::get_if<Record>(&args.at(0))) {
            co_return integer(
                static_cast<std::int64_t>(record_value->v ? record_value->v->fields.size() : 0));
        }
        co_return fail_result(
            runtime_failure("len is not defined for " + std::string{kind_name(args.at(0))},
                            program_, span, "call"));
    }
    if (builtin == "usd") {
        if (const auto* amount = std::get_if<Float>(&args.at(0))) {
            co_return money(spec::Money::usd(amount->v));
        }
        if (const auto* amount = std::get_if<Int>(&args.at(0))) {
            co_return money(spec::Money::usd(static_cast<double>(amount->v)));
        }
        co_return fail_result(
            runtime_failure("usd expects Float, found " + std::string{kind_name(args.at(0))},
                            program_, span, "call"));
    }
    if (builtin == "to_string") {
        co_return string(display(args.at(0)));
    }
    if (builtin == "trim") {
        if (const auto* text = std::get_if<Str>(&args.at(0)); (text != nullptr) && text->v) {
            constexpr std::string_view whitespace = " \t\r\n";
            const std::string& source = *text->v;
            const std::size_t first = source.find_first_not_of(whitespace);
            if (first == std::string::npos) {
                co_return string({});
            }
            const std::size_t last = source.find_last_not_of(whitespace);
            co_return string(source.substr(first, last - first + 1));
        }
        co_return fail_result(runtime_failure("trim expects String", program_, span, "call"));
    }
    if (builtin == "lines") {
        if (const auto* text = std::get_if<Str>(&args.at(0)); (text != nullptr) && text->v) {
            std::vector<Value> lines_out;
            std::string current;
            for (const char character : *text->v) {
                if (character == '\n') {
                    if (!current.empty() && current.back() == '\r') {
                        current.pop_back();
                    }
                    lines_out.push_back(string(std::move(current)));
                    current.clear();
                } else {
                    current.push_back(character);
                }
            }
            if (!current.empty()) {
                if (current.back() == '\r') {
                    current.pop_back();
                }
                lines_out.push_back(string(std::move(current)));
            }
            co_return list(std::move(lines_out));
        }
        co_return fail_result(runtime_failure("lines expects String", program_, span, "call"));
    }
    if (builtin == "contains") {
        if (const auto* haystack = std::get_if<Str>(&args.at(0))) {
            const auto* needle = std::get_if<Str>(&args.at(1));
            if (needle == nullptr) {
                co_return fail_result(
                    runtime_failure("contains expects a String needle", program_, span, "call"));
            }
            const std::string empty;
            const std::string& hay = haystack->v ? *haystack->v : empty;
            const std::string& pin = needle->v ? *needle->v : empty;
            co_return boolean(hay.contains(pin));
        }
        if (const auto* items = std::get_if<List>(&args.at(0)); (items != nullptr) && items->v) {
            bool found = false;
            for (const Value& item : items->v->items) {
                if (structurally_equal(item, args.at(1))) {
                    found = true;
                    break;
                }
            }
            co_return boolean(found);
        }
        co_return fail_result(runtime_failure("contains expects String or List, found " +
                                                  std::string{kind_name(args.at(0))},
                                              program_, span, "call"));
    }
    if (builtin == "first") {
        if (const auto* items = std::get_if<List>(&args.at(0)); (items != nullptr) && items->v) {
            if (items->v->items.empty()) {
                co_return null();
            }
            co_return items->v->items.front();
        }
        co_return fail_result(
            runtime_failure("first expects List, found " + std::string{kind_name(args.at(0))},
                            program_, span, "call"));
    }
    if (builtin == "filter" || builtin == "map" || builtin == "any" || builtin == "all") {
        const auto* items = std::get_if<List>(&args.at(0));
        if (items == nullptr) {
            co_return fail_result(runtime_failure(builtin + " expects a List, found " +
                                                      std::string{kind_name(args.at(0))},
                                                  program_, span, "call"));
        }
        const Value& callable = args.at(1);
        if (!std::holds_alternative<Fn>(callable)) {
            co_return fail_result(runtime_failure(builtin + " expects a function, found " +
                                                      std::string{kind_name(callable)},
                                                  program_, span, "call"));
        }
        std::vector<Value> results;
        bool any_true = false;
        const std::vector<Value> no_items;
        const std::vector<Value>& elements = items->v ? items->v->items : no_items;
        for (const Value& item : elements) {
            auto mapped = co_await call(callable, {item}, {});
            if (!mapped) {
                co_return std::unexpected(std::move(mapped).error());
            }
            if (builtin == "map") {
                results.push_back(std::move(*mapped));
                continue;
            }
            const auto* predicate = std::get_if<Bool>(&*mapped);
            if (predicate == nullptr) {
                co_return fail_result(runtime_failure(builtin +
                                                          " predicate must return Bool, "
                                                          "found " +
                                                          std::string{kind_name(*mapped)},
                                                      program_, span, "call"));
            }
            if (builtin == "filter") {
                if (predicate->v) {
                    results.push_back(item);
                }
            } else if (builtin == "any") {
                if (predicate->v) {
                    any_true = true;
                    break;
                }
            } else if (!predicate->v) {
                any_true = true;
                break;
            }
        }
        if (builtin == "any") {
            co_return boolean(any_true);
        }
        if (builtin == "all") {
            co_return boolean(!any_true);
        }
        co_return list(std::move(results));
    }

    co_return fail_result(
        runtime_failure("unknown built-in '" + builtin + "'", program_, span, "call"));
}

asio::awaitable<std::expected<Value, Failure>> Interpreter::run_flow(std::string_view name,
                                                                     Value input) {
    const Flow* flow = program_.find_flow(name);
    if (flow == nullptr) {
        co_return std::unexpected(runtime_failure("unknown flow '" + std::string{name} + "'",
                                                  program_, syntax::Span(), "flow"));
    }
    Env scope = root_env_.child();
    scope = scope.define(flow->param.name, std::move(input));
    auto result = co_await eval_block(&flow->body, scope);
    if (!result) {
        Control control = std::move(result).error();
        if (auto* failure = std::get_if<Fail>(&control)) {
            co_return std::unexpected(std::move(failure->failure));
        }
        co_return std::move(std::get<Complete>(control).value);
    }
    co_return std::move(*result);
}

} // namespace leash::runtime
