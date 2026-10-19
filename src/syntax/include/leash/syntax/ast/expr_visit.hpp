// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <utility>
#include <variant>

#include "leash/syntax/ast.hpp"

namespace leash::syntax::ast {

/// The single visitation point for `Expr`. Code outside `src/syntax` must use
/// this helper or `std::visit` on `Expr::kind`; do not open-code the layout.
template <typename Visitor> decltype(auto) visit_expr(Visitor&& visitor, const Expr& expr) {
    return std::visit(std::forward<Visitor>(visitor), expr.kind);
}

} // namespace leash::syntax::ast
