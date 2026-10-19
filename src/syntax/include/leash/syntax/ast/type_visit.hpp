// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <utility>
#include <variant>

#include "leash/syntax/ast.hpp"

namespace leash::syntax::ast {

/// The single visitation point for `TypeExpr`.
template <typename Visitor> decltype(auto) visit_type(Visitor&& visitor, const TypeExpr& type) {
    return std::visit(std::forward<Visitor>(visitor), type.kind);
}

} // namespace leash::syntax::ast
