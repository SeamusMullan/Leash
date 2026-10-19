// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <utility>
#include <variant>

#include "leash/syntax/ast.hpp"

namespace leash::syntax::ast {

/// The single visitation point for `Item`.
template <typename Visitor> decltype(auto) visit_item(Visitor&& visitor, const Item& item) {
    return std::visit(std::forward<Visitor>(visitor), item);
}

} // namespace leash::syntax::ast
