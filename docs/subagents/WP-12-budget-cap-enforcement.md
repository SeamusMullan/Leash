# WP-12: Budget and capability runtime enforcement

Milestone: M3 · Size: M · Status: see `docs/status.md`

## Goal
After this WP, the runtime is the authority the design promises: budgets and
capability sets are lexically scoped values on a scope stack, every effect is
checked before it runs, consumption is charged upward, wall deadlines cancel
in-flight effects, out-of-scope writes by CLI drivers are reverted, and every
journal entry carries `budget_after`. Drivers receive a translation of the
active capability set to their own controls.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `docs/07-budgets-and-capabilities.md` (all; §1 budgets, §2 capabilities, §2.3 enforcement points, §3 gates, §5 limits)
- `docs/02-language-spec.md` §8 (budgets and capabilities), §9.6 (runtime failure kinds)
- `docs/03-runtime-architecture.md` §6.3 (scopes), §7 (effect gateway)
- `docs/04-determinism-and-replay.md` §3 (journal entry fields `caps`, `budget_after`)
- `docs/09-drivers-and-tools.md` §3, §4 (flag translation targets)
- ADR-0007; WP-11 (`leash_policy`, `RunWorkspace::diff/restore`); WP-04 (gateway, `EffectCtx`); WP-02 (`std/prices.toml`)

## Scope
- `Budget`, `Usage`, `Money` (fixed-point, 6 decimals, USD) types in `leash_spec`; `Pricing` loaded from `std/prices.toml` through toml++ with `leash.toml` overrides; `Money Pricing::cost(std::string_view model, const Usage&) const`; unknown model → one `spdlog::warn` per run, tokens still counted.
- `BudgetStack`: push/pop scopes (task root, `Scope { kind: Budget }` LIR nodes, `ask`/`run` call scopes, agent defaults); entry check against parent *remaining* (fail `Budget` at entry, spec §8.2); `reserve(counter)` before an effect for `tool_calls`/`llm_calls`/`turns`; `charge(usage)` after each model turn and tool call, propagated to every enclosing scope; `remaining()`; `deadline()` = min over the stack of `started + wall`; `snapshot()` for `budget_after`.
- Wall enforcement: the effect gateway arms one `asio::steady_timer` at `deadline()` inside the clock effect; on expiry it raises the handler's `asio::cancellation_signal`, journals `status: "cancelled"` with `failure.kind = Budget`, and raises `Budget { limit: "wall" }`.
- Token/cost enforcement: after each turn (`ask` or a turn inside `run`) compare; when exceeded, the LLM handler cancels the run and raises `Budget` with `data: { limit, consumed, allowed, scope }`; the overshoot is charged.
- `CapStack`: push/pop (`Scope { kind: Allow }`, task root from `allow`/`deny`, agent `allow`, call `allow`); provable-violation check at entry (same rules as WP-09 E0701) raising `Permission`; **effective permission = every scope on the stack permits** (intersection), so unprovable narrowing is still safe; `deny` subtracts and cannot be re-allowed.
- Enforcement wiring in the gateway/tool layer: `fs.*` via `path_permitted` for read/write; `shell`/`test` via `command_permitted`; `http.*`/MCP-over-HTTP via `host_permitted`; `gate`/`ask_human` require `human`; `parallel`/`race`/`map(concurrency>1)`/`spawn` require `spawn`; `env.get` requires the variable in `env`; `secrets.get` requires the name in `secrets`; violations raise `Permission` with `data: { cap, requested, scope_site }` and are journaled as effects with `status: "denied"`.
- Journal: `caps` (active effective set, rendered compactly) and `budget_after` on every entry; `scope.enter`/`scope.exit` journal entries carrying the spec and consumption (needed by `leash trace --budget`; add to `spec/journal.schema.json`).
- Post-hoc diff check after every `run` whose driver does not route tool calls through the runtime (CLI drivers), and optionally after all runs (`[runs] post_check = "always"`): diff against the pre-run snapshot; paths outside `fs.write` are restored from the snapshot (new files deleted), each journaled as a `permission.violation` entry; then fail with `Permission` unless `[runs] on_violation = "revert-and-continue"`.
- `DriverCapTranslation` and `translate(const CapSet&, const Budget&) -> DriverCapTranslation` producing Claude Code flags (`--allowedTools` entries like `Bash(npm test*)`, `Read`, `Edit`, `Write`, `Grep`, `Glob`; `--disallowedTools WebFetch WebSearch` unless `net` non-empty; `--add-dir` for roots outside the workspace, which v1 never grants; `--permission-mode`; `--max-budget-usd` from remaining cost), Codex settings (`--sandbox read-only|workspace-write`, `--add-dir`), and a list of `unenforceable` items (e.g. subprocess network egress) that the runtime prints at run start as warnings.
- `leash trace <run> --budget`: tree of scopes from `scope.*` entries with limits, consumed and children.

## Out of scope
- Static checks (WP-09). Drivers themselves (WP-13/18/19 consume `DriverCapTranslation`). Gate policies and TTY prompting (WP-22 owns UX; this WP only enforces `human`).
- Human-time budgets and webhook gates (post-v1).

## Interfaces
```cpp
// src/runtime/include/leash/runtime/scope/budget.hpp
// Money, Budget, CapSet and Usage are leash_spec types (WP-02); this WP adds the
// stacks and the accounting only.
struct Consumption {                    // derived from leash_spec Usage by the gateway
    std::uint64_t tokens = 0;
    Money cost;
    std::uint32_t tool_calls = 0;
    std::uint32_t llm_calls = 0;
    std::uint32_t turns = 0;
    std::chrono::nanoseconds wall{0};
};

enum class Counter { tool_call, llm_call, turn };

class BudgetStack {
public:
    std::expected<ScopeId, Failure> push(SiteId site, const Budget& limits);
    ScopeSummary pop(ScopeId id);

    std::expected<void, Failure> reserve(Counter c);
    std::expected<void, Failure> charge(const Usage& usage);

    Budget remaining() const;
    std::optional<std::chrono::steady_clock::time_point> deadline() const;
    BudgetAfter snapshot() const;
};

// src/spec/include/leash/spec/cap_set.hpp — the accounting methods are added here
class CapSet {
public:
    static CapSet read_only();
    static CapSet default_for_task();                              // one constant; see notes
    std::optional<CapViolation> provable_violation(const CapSet& parent) const;
    CapSet minus(const CapSet& deny) const;
    nlohmann::json render_compact() const;

    Globs fs_read;
    Globs fs_write;
    std::vector<ShellPattern> shell;
    std::vector<HostPattern> net;
    bool human = false;
    bool spawn = false;
    std::vector<std::string> env;
    std::vector<std::string> secrets;
};

// src/runtime/include/leash/runtime/scope/caps.hpp
class CapStack {
public:
    std::expected<ScopeId, Failure> push(SiteId site, CapSet caps);
    void pop(ScopeId id);

    bool permits_path(const std::filesystem::path& rel, Access access) const;      // every scope
    std::expected<Permit, Failure> permits_command(std::string_view cmd) const;    // every scope; Permit from leash_policy
    bool permits_host(std::string_view host) const;
    bool permits(CapFlag flag) const;
    CapSet effective() const;
};

struct DriverCapTranslation {
    ClaudeCodeFlags claude_code;
    CodexFlags codex;
    std::vector<std::string> notes;
    std::vector<std::string> unenforceable;
};
DriverCapTranslation translate(const CapSet& caps, const Budget& remaining);

class PostRunCheck {
public:
    static std::expected<std::vector<Violation>, Failure> run(const RunWorkspace& ws, const CapSet& caps,
                                                          const SnapshotRef& before, Journal& journal,
                                                          OnViolation policy);
};
```
`EffectCtx` (WP-04) gains `BudgetStack& budget`, `CapStack& caps` and
`std::optional<std::chrono::steady_clock::time_point> deadline`.

## Deliverables
- `src/runtime/scope/{budget,caps,pricing,translate,postcheck}.cpp` with headers under `src/runtime/include/leash/runtime/scope/`; gateway changes; journal schema update; `leash trace --budget`.
- `std/prices.toml` populated for `claude-opus-5`, `claude-sonnet-5`, `claude-haiku-4-5`, `claude-fable-5-1` from `docs/research`/API reference values (cite the date in a comment).
- Tests: `tests/runtime/{budget_scopes,budget_wall,budget_tokens,caps_stack,postcheck}_test.cpp`, `tests/runtime/translate_test.cpp` (golden flags for three `CapSet`s through `expect_golden`), `tests/cli/trace_budget_test.cpp`; `budget_tokens` uses a mock driver reporting usage.

## Acceptance criteria
- `ctest --preset dev -R 'leash_(runtime|effects)'` → green.
- Test `nested_scope_exceeds_parent`: task `budget { cost: usd(1) }`, inner `budget { cost: usd(5) } { … }` → `Budget` at scope entry with `data.field = "cost"`, `data.requested = "$5.0000"`, `data.remaining = "$1.0000"`; no effect inside the block runs (journal has no entries after `scope.enter`).
- Test `wall_cancels_effect`: `budget { wall: 1s } { ask slow("…") }` with a mock driver `latency_ms: 5000` → the journal entry for the ask has `status: "cancelled"`, the failure kind is `Budget` with `limit: "wall"`, and the test completes in under 3 s.
- Test `tokens_exceeded_mid_run`: mock `run` emitting 3 turns of 10k tokens under `tokens: 15_000` → run cancelled after turn 2, `Budget { limit: "tokens" }`, `budget_after.tokens` shows the overshoot charged.
- Test `write_outside_allow_reverted`: a mock CLI-style run writes `src/ok.js` and `README.md` under `fs.write: ["src/**"]` → `README.md` restored to the snapshot content, `permission.violation` entry in the journal naming `README.md`, run fails with `Permission`; with `on_violation = "revert-and-continue"` the run continues and completes.
- Test `gate_requires_human`: `gate("x")` under a task without `human: true` → `Permission { cap: "human" }`.
- `leash trace <run> --budget` on the `02_fix_bug` mock fixture prints a tree with the task root, the retry attempt scopes and per-scope `cost`/`tokens`/`tool_calls`.
- `leash run --driver anthropic=mock tests/fixtures/budget_tree.leash` exits with code 4 when the budget fails and prints the scope site.

## Notes and pitfalls
- Intersection semantics are the safety net: `permits_*` must consult every scope, not just the innermost; the provable-violation check at entry is an early error, nothing more.
- Wall deadline is the minimum over the stack; compute it once per effect, not per poll.
- Charging happens after the effect, but `reserve` happens before; both must be reflected in `budget_after` of the same journal entry (write the finished entry after charging).
- `Usage` from Claude Code carries `total_cost_usd`; reconcile by taking the reported cost when present and noting the delta versus the price table (WP-18 supplies the value; this WP provides `Usage::with_reported_cost`).
- Secrets: `secrets.get` returns a tagged string; enforcement here only checks the capability; redaction is in the journal writer (WP-04/06). Never log values in `Permission` data.
- `spawn: true` gating of concurrency follows docs/07 §2.4; the architect is reviewing whether `spawn` should default to `true`. Implement the gate; the default lives in one constant (`CapSet::default_for_task`) so the decision is a one-line change.
- Keep `translate` free of I/O; drivers call it and add their own version-specific flag names.

## Dependencies
Hard: WP-04, WP-11. Soft: WP-02 (journal schema, prices), WP-06 (journal writer fields).

## Estimated size
M (2–4 days).
