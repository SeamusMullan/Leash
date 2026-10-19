// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/syntax/ast.hpp"

#include <utility>
#include <variant>

namespace leash::syntax::ast {
namespace {

[[nodiscard]] bool same(const ExprPtr& lhs, const ExprPtr& rhs) {
    if (!lhs || !rhs) {
        return !lhs && !rhs;
    }
    return *lhs == *rhs;
}

[[nodiscard]] bool same(const std::optional<ExprPtr>& lhs, const std::optional<ExprPtr>& rhs) {
    if (!lhs.has_value() || !rhs.has_value()) {
        return lhs.has_value() == rhs.has_value();
    }
    return same(*lhs, *rhs);
}

[[nodiscard]] bool same(const TypeExprPtr& lhs, const TypeExprPtr& rhs) {
    if (!lhs || !rhs) {
        return !lhs && !rhs;
    }
    return *lhs == *rhs;
}

[[nodiscard]] bool same(const Arg& lhs, const Arg& rhs) {
    if (lhs.index() != rhs.index()) {
        return false;
    }
    if (const auto* expr = std::get_if<ExprPtr>(&lhs)) {
        return same(*expr, std::get<ExprPtr>(rhs));
    }
    return std::get<NamedArg>(lhs) == std::get<NamedArg>(rhs);
}

[[nodiscard]] bool same(const std::vector<InterpPart>& lhs, const std::vector<InterpPart>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs.at(index).index() != rhs.at(index).index()) {
            return false;
        }
        if (const auto* text = std::get_if<std::string_view>(&lhs.at(index))) {
            if (*text != std::get<std::string_view>(rhs.at(index))) {
                return false;
            }
        } else if (!same(std::get<ExprPtr>(lhs.at(index)), std::get<ExprPtr>(rhs.at(index)))) {
            return false;
        }
    }
    return true;
}

} // namespace

bool RecordLit::operator==(const RecordLit& other) const {
    if (fields.size() != other.fields.size()) {
        return false;
    }
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (!(fields.at(index).name == other.fields.at(index).name) ||
            !same(fields.at(index).value, other.fields.at(index).value)) {
            return false;
        }
    }
    return true;
}

bool NamedRecord::operator==(const NamedRecord& other) const {
    return type == other.type && record == other.record;
}

bool ListLit::operator==(const ListLit& other) const {
    if (elems.size() != other.elems.size()) {
        return false;
    }
    for (std::size_t index = 0; index < elems.size(); ++index) {
        if (!same(elems.at(index), other.elems.at(index))) {
            return false;
        }
    }
    return true;
}

bool LetStmt::operator==(const LetStmt& other) const {
    return name == other.name && same(type, other.type) && same(value, other.value);
}

bool ExprStmt::operator==(const ExprStmt& other) const { return same(expr, other.expr); }

bool Block::operator==(const Block& other) const {
    return stmts == other.stmts && same(tail, other.tail);
}

bool IfExpr::operator==(const IfExpr& other) const {
    return same(cond, other.cond) && then_block == other.then_block &&
           same(else_branch, other.else_branch);
}

bool Unary::operator==(const Unary& other) const {
    return op == other.op && same(operand, other.operand);
}

bool Binary::operator==(const Binary& other) const {
    return op == other.op && same(lhs, other.lhs) && same(rhs, other.rhs);
}

bool NamedArg::operator==(const NamedArg& other) const {
    return name == other.name && same(value, other.value);
}

bool Call::operator==(const Call& other) const {
    if (!same(callee, other.callee) || args.size() != other.args.size() ||
        !same(trailing, other.trailing)) {
        return false;
    }
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (!same(args.at(index), other.args.at(index))) {
            return false;
        }
    }
    return true;
}

bool Method::operator==(const Method& other) const {
    if (!same(receiver, other.receiver) || !(name == other.name) ||
        args.size() != other.args.size() || !same(trailing, other.trailing)) {
        return false;
    }
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (!same(args.at(index), other.args.at(index))) {
            return false;
        }
    }
    return true;
}

bool Field::operator==(const Field& other) const {
    return same(receiver, other.receiver) && name == other.name && optional == other.optional;
}

bool Lambda::operator==(const Lambda& other) const {
    return params == other.params && parenthesised == other.parenthesised &&
           trailing == other.trailing && same(body, other.body);
}

bool Ask::operator==(const Ask& other) const {
    if (!same(target, other.target) || args.size() != other.args.size()) {
        return false;
    }
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (!same(args.at(index), other.args.at(index))) {
            return false;
        }
    }
    return true;
}

bool FailExpr::operator==(const FailExpr& other) const {
    if (!(kind == other.kind) || args.size() != other.args.size()) {
        return false;
    }
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (!same(args.at(index), other.args.at(index))) {
            return false;
        }
    }
    return true;
}

bool Complete::operator==(const Complete& other) const { return same(value, other.value); }

bool Interp::operator==(const Interp& other) const {
    return open == other.open && close == other.close && same(parts, other.parts);
}

bool ListType::operator==(const ListType& other) const {
    return elem && other.elem && *elem == *other.elem;
}

bool OptionalType::operator==(const OptionalType& other) const {
    return inner && other.inner && *inner == *other.inner;
}

bool RecordField::operator==(const RecordField& other) const {
    return name == other.name && type && other.type && *type == *other.type &&
           same(refine, other.refine);
}

bool RecordType::operator==(const RecordType& other) const { return fields == other.fields; }

bool UseDecl::operator==(const UseDecl& other) const {
    return path == other.path && grouped == other.grouped && names == other.names;
}

bool ConstDecl::operator==(const ConstDecl& other) const {
    return is_pub == other.is_pub && name == other.name && same(type, other.type) &&
           same(value, other.value);
}

bool TypeDecl::operator==(const TypeDecl& other) const {
    return is_pub == other.is_pub && name == other.name && same(type, other.type);
}

bool Param::operator==(const Param& other) const {
    return name == other.name && same(type, other.type) && same(default_value, other.default_value);
}

bool FnDecl::operator==(const FnDecl& other) const {
    return is_pub == other.is_pub && name == other.name && params == other.params &&
           same(ret, other.ret) && effects == other.effects && body == other.body;
}

bool AgentDecl::operator==(const AgentDecl& other) const {
    return is_pub == other.is_pub && name == other.name && same(value, other.value);
}

bool TaskOutput::operator==(const TaskOutput& other) const {
    return type && other.type && *type == *other.type;
}

bool TaskBudget::operator==(const TaskBudget& other) const { return same(value, other.value); }

bool TaskDecl::operator==(const TaskDecl& other) const {
    return is_pub == other.is_pub && name == other.name && sections == other.sections;
}

bool FlowDecl::operator==(const FlowDecl& other) const {
    return is_pub == other.is_pub && name == other.name && param == other.param &&
           same(ret, other.ret) && effects == other.effects && body == other.body;
}

bool Program::operator==(const Program& other) const {
    if (module_path.has_value() != other.module_path.has_value()) {
        return false;
    }
    if (module_path && !(*module_path == *other.module_path)) {
        return false;
    }
    return items == other.items;
}

NodeId node_id(const Item& item) {
    return std::visit([](const auto& node) { return node.id; }, item);
}

Span node_span(const Item& item) {
    return std::visit([](const auto& node) { return node.span; }, item);
}

NodeId node_id(const Expr& expr) { return expr.id; }

Span node_span(const Expr& expr) { return expr.span; }

NodeId node_id(const TypeExpr& type) { return type.id; }

Span node_span(const TypeExpr& type) { return type.span; }

NodeId node_id(const Stmt& stmt) {
    return std::visit([](const auto& node) { return node.id; }, stmt);
}

Span node_span(const Stmt& stmt) {
    return std::visit([](const auto& node) { return node.span; }, stmt);
}

NodeId node_id(const Program& program) { return program.id; }

} // namespace leash::syntax::ast
