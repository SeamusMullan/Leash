// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "leash/runtime/program.hpp"
#include "leash/runtime/value.hpp"

namespace leash::runtime {

/// One immutable scope frame. `define` never mutates a frame: it copies the
/// binding map into a new frame, so a closure that captured the old `Env` is
/// unaffected by later bindings.
struct Frame {
    std::map<std::string, Value, std::less<>> bindings;
    std::shared_ptr<const Frame> parent;
};

/// A lexical environment. Copies share frames and are cheap. Bindings are
/// immutable; there is no assignment in v0's evaluator.
class Env {
  public:
    Env() = default;

    /// A new empty scope nested inside this one.
    [[nodiscard]] Env child() const;

    /// A new scope that contains this scope's bindings plus `name`.
    [[nodiscard]] Env define(std::string name, Value value) const;

    [[nodiscard]] std::optional<Value> lookup(std::string_view name) const;
    [[nodiscard]] bool contains(std::string_view name) const;

  private:
    explicit Env(std::shared_ptr<const Frame> frame) : frame_(std::move(frame)) {}

    std::shared_ptr<const Frame> frame_;
};

/// A callable value. User closures capture their defining environment by value.
/// Function and lambda bodies are owned by `Program`, which must outlive the
/// interpreter and every closure it creates.
struct Closure {
    std::string name;
    std::vector<std::string> params;
    /// Default expressions, aligned with `params`; null when a parameter has
    /// no default.
    std::vector<const Expr*> defaults;
    /// Exactly one of `block` (fn/flow body) or `expr` (lambda body) is set.
    const Block* block = nullptr;
    const Expr* expr = nullptr;
    Env env;
    /// Non-empty for a built-in; the evaluator dispatches on this name.
    std::string builtin;
};

} // namespace leash::runtime
