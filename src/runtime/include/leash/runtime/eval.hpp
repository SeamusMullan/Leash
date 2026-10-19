// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#pragma once

#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

#include "leash/runtime/env.hpp"
#include "leash/runtime/failure.hpp"
#include "leash/runtime/program.hpp"
#include "leash/runtime/value.hpp"

namespace leash::runtime {

namespace asio = boost::asio;

/// A normal early exit from a flow. v0 has no `break` or `return`; WP-07 adds
/// those alternatives to `Control`.
struct Complete {
    Value value;
};

/// The error channel of the evaluator: a real failure or a non-local transfer.
using Control = std::variant<Fail, Complete>;

/// Tree-walking evaluator over the v0 `Program` model. `Program` must outlive
/// the interpreter; closures hold non-owning pointers to its bodies.
///
/// Pure expressions are evaluated inside the coroutine without suspending.
/// Stage 1 has no effect gateway, so `ask` fails with an `Unknown` failure;
/// stage 2 replaces that arm with an `EffectGateway` call.
class Interpreter {
  public:
    explicit Interpreter(const Program& program);

    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;
    Interpreter(Interpreter&&) = delete;
    Interpreter& operator=(Interpreter&&) = delete;
    ~Interpreter() = default;

    /// Evaluate against the root environment (built-ins only). `expr` must
    /// outlive the awaited coroutine; it normally belongs to `Program`.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
    [[nodiscard]] asio::awaitable<std::expected<Value, Control>> eval(const Expr& expr);

    /// Bind `input` to the flow's parameter and run its body. `Complete` and a
    /// block result are both normal returns.
    [[nodiscard]] asio::awaitable<std::expected<Value, Failure>> run_flow(std::string_view name,
                                                                          Value input);

    /// Invoke a callable value with evaluated arguments. Used by built-ins
    /// (`filter`, `map`, ...) and available to the runner.
    [[nodiscard]] asio::awaitable<std::expected<Value, Control>>
    call(Value callee, std::vector<Value> positional,
         std::map<std::string, Value, std::less<>> named = {});

    [[nodiscard]] const Env& root_env() const noexcept { return root_env_; }

  private:
    [[nodiscard]] asio::awaitable<std::expected<Value, Control>> eval_impl(const Expr* expr,
                                                                           Env env);
    [[nodiscard]] asio::awaitable<std::expected<Value, Control>> eval_block(const Block* block,
                                                                            Env env);
    [[nodiscard]] asio::awaitable<std::expected<Value, Control>> eval_string(const StringLit* lit,
                                                                             Env env);
    [[nodiscard]] asio::awaitable<std::expected<Value, Control>> eval_name(const NameExpr* name,
                                                                           Env env);
    [[nodiscard]] asio::awaitable<std::expected<Value, Control>> eval_call(const CallExpr* call,
                                                                           Env env);
    [[nodiscard]] asio::awaitable<std::expected<
        std::pair<std::vector<Value>, std::map<std::string, Value, std::less<>>>, Control>>
    eval_args(const std::vector<CallArg>* args, Env env);
    [[nodiscard]] asio::awaitable<std::expected<Value, Control>>
    eval_method(const MethodExpr* method, Env env);
    [[nodiscard]] asio::awaitable<std::expected<Value, Control>>
    call_closure(std::shared_ptr<const Closure> closure, std::vector<Value> positional,
                 std::map<std::string, Value, std::less<>> named, syntax::Span span);
    [[nodiscard]] asio::awaitable<std::expected<Value, Control>>
    call_builtin(std::shared_ptr<const Closure> closure, std::vector<Value> positional,
                 std::map<std::string, Value, std::less<>> named, syntax::Span span);

    [[nodiscard]] std::optional<Value> lookup_callable(std::string_view name, const Env& env) const;
    [[nodiscard]] Closure make_fn_closure(const FnDecl& function_decl) const;
    [[nodiscard]] Closure make_flow_closure(const Flow& flow) const;
    [[nodiscard]] static Value make_builtin(const std::string& name,
                                            std::vector<std::string> params);

    const Program& program_;
    Env root_env_;
};

} // namespace leash::runtime
