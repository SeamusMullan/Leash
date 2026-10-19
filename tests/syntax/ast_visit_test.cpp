// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "leash/syntax/ast/expr_visit.hpp"
#include "leash/syntax/ast/item_visit.hpp"
#include "leash/syntax/ast/type_visit.hpp"

namespace {

using namespace leash::syntax::ast;

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
template <typename T> [[nodiscard]] std::string_view name_of() {
    if constexpr (std::is_same_v<T, Lit>) {
        return "Lit";
    } else if constexpr (std::is_same_v<T, Path>) {
        return "Path";
    } else if constexpr (std::is_same_v<T, RecordLit>) {
        return "RecordLit";
    } else if constexpr (std::is_same_v<T, NamedRecord>) {
        return "NamedRecord";
    } else if constexpr (std::is_same_v<T, ListLit>) {
        return "ListLit";
    } else if constexpr (std::is_same_v<T, Variant>) {
        return "Variant";
    } else if constexpr (std::is_same_v<T, IfExpr>) {
        return "IfExpr";
    } else if constexpr (std::is_same_v<T, Block>) {
        return "Block";
    } else if constexpr (std::is_same_v<T, Unary>) {
        return "Unary";
    } else if constexpr (std::is_same_v<T, Binary>) {
        return "Binary";
    } else if constexpr (std::is_same_v<T, Call>) {
        return "Call";
    } else if constexpr (std::is_same_v<T, Method>) {
        return "Method";
    } else if constexpr (std::is_same_v<T, Field>) {
        return "Field";
    } else if constexpr (std::is_same_v<T, Lambda>) {
        return "Lambda";
    } else if constexpr (std::is_same_v<T, Ask>) {
        return "Ask";
    } else if constexpr (std::is_same_v<T, FailExpr>) {
        return "FailExpr";
    } else if constexpr (std::is_same_v<T, Complete>) {
        return "Complete";
    } else if constexpr (std::is_same_v<T, Interp>) {
        return "Interp";
    } else if constexpr (std::is_same_v<T, UseDecl>) {
        return "UseDecl";
    } else if constexpr (std::is_same_v<T, ConstDecl>) {
        return "ConstDecl";
    } else if constexpr (std::is_same_v<T, TypeDecl>) {
        return "TypeDecl";
    } else if constexpr (std::is_same_v<T, FnDecl>) {
        return "FnDecl";
    } else if constexpr (std::is_same_v<T, AgentDecl>) {
        return "AgentDecl";
    } else if constexpr (std::is_same_v<T, TaskDecl>) {
        return "TaskDecl";
    } else if constexpr (std::is_same_v<T, FlowDecl>) {
        return "FlowDecl";
    } else if constexpr (std::is_same_v<T, NamedType>) {
        return "NamedType";
    } else if constexpr (std::is_same_v<T, ListType>) {
        return "ListType";
    } else if constexpr (std::is_same_v<T, OptionalType>) {
        return "OptionalType";
    } else if constexpr (std::is_same_v<T, RecordType>) {
        return "RecordType";
    } else {
        return "unknown";
    }
}

} // namespace

TEST_CASE("expr visitor visits every alternative", "[syntax][ast]") {
    Expr expr;
    std::vector<std::string_view> visited;
    const auto visitor = [&visited](const auto& node) {
        visited.push_back(name_of<std::decay_t<decltype(node)>>());
    };

    expr.kind = Lit{};
    visit_expr(visitor, expr);
    expr.kind = Path{};
    visit_expr(visitor, expr);
    expr.kind = RecordLit{};
    visit_expr(visitor, expr);
    expr.kind = NamedRecord{};
    visit_expr(visitor, expr);
    expr.kind = ListLit{};
    visit_expr(visitor, expr);
    expr.kind = Variant{};
    visit_expr(visitor, expr);
    expr.kind = IfExpr{};
    visit_expr(visitor, expr);
    expr.kind = Block{};
    visit_expr(visitor, expr);
    expr.kind = Unary{};
    visit_expr(visitor, expr);
    expr.kind = Binary{};
    visit_expr(visitor, expr);
    expr.kind = Call{};
    visit_expr(visitor, expr);
    expr.kind = Method{};
    visit_expr(visitor, expr);
    expr.kind = Field{};
    visit_expr(visitor, expr);
    expr.kind = Lambda{};
    visit_expr(visitor, expr);
    expr.kind = Ask{};
    visit_expr(visitor, expr);
    expr.kind = FailExpr{};
    visit_expr(visitor, expr);
    expr.kind = Complete{};
    visit_expr(visitor, expr);
    expr.kind = Interp{};
    visit_expr(visitor, expr);

    CHECK(visited.size() == 18);
    CHECK(visited.front() == "Lit");
    CHECK(visited.back() == "Interp");
}

TEST_CASE("item visitor visits every alternative", "[syntax][ast]") {
    std::vector<std::string_view> visited;
    const auto visitor = [&visited](const auto& node) {
        visited.push_back(name_of<std::decay_t<decltype(node)>>());
    };

    Item item = UseDecl{};
    visit_item(visitor, item);
    item = ConstDecl{};
    visit_item(visitor, item);
    item = TypeDecl{};
    visit_item(visitor, item);
    item = FnDecl{};
    visit_item(visitor, item);
    item = AgentDecl{};
    visit_item(visitor, item);
    item = TaskDecl{};
    visit_item(visitor, item);
    item = FlowDecl{};
    visit_item(visitor, item);

    CHECK(visited.size() == 7);
    CHECK(visited.front() == "UseDecl");
    CHECK(visited.back() == "FlowDecl");
}

TEST_CASE("type visitor visits every alternative", "[syntax][ast]") {
    TypeExpr type;
    std::vector<std::string_view> visited;
    const auto visitor = [&visited](const auto& node) {
        visited.push_back(name_of<std::decay_t<decltype(node)>>());
    };

    type.kind = NamedType{};
    visit_type(visitor, type);
    type.kind = ListType{};
    visit_type(visitor, type);
    type.kind = OptionalType{};
    visit_type(visitor, type);
    type.kind = RecordType{};
    visit_type(visitor, type);

    CHECK(visited.size() == 4);
    CHECK(visited.front() == "NamedType");
    CHECK(visited.back() == "RecordType");
}
