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
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/07-budgets-and-capabilities.md` (all; §1 budgets, §2 capabilities, §2.3 enforcement points, §3 gates, §5 limits)
- `docs/02-language-spec.md` §8 (budgets and capabilities), §9.6 (runtime failure kinds)
- `docs/03-runtime-architecture.md` §6.3 (scopes), §7 (effect gateway)
- `docs/04-determinism-and-replay.md` §3 (journal entry fields `caps`, `budget_after`)
- `docs/09-drivers-and-tools.md` §3, §4 (flag translation targets)
- ADR-0007; WP-11 (`leash-policy`, `Workspace::diff/restore`); WP-04 (gateway, `EffectCtx`); WP-02 (`std/prices.toml`)

## Scope
- `Budget`, `Usage`, `Money` (fixed-point, 6 decimals, USD) types; `Pricing` loaded from `std/prices.toml` with `leash.toml` overrides; `Pricing::cost(model, &Usage) -> Money`; unknown model → one warning per run, tokens still counted.
- `BudgetStack`: push/pop scopes (task root, `Scope { kind: Budget }` LIR nodes, `ask`/`run` call scopes, agent defaults); entry check against parent *remaining* (fail `Budget` at entry, spec §8.2); `reserve(counter)` before an effect for `tool_calls`/`llm_calls`/`turns`; `charge(usage)` after each model turn and tool call, propagated to every enclosing scope; `remaining()`; `deadline()` = min over the stack of `started + wall`; `snapshot()` for `budget_after`.
- Wall enforcement: the effect gateway wraps `perform` in `timeout_at(deadline)`; on expiry it cancels the handler (drivers/subprocesses get `cancel`), journals `status: "cancelled"` with `failure.kind = Budget`, and raises `Budget { limit: "wall" }`.
- Token/cost enforcement: after each turn (`ask` or a turn inside `run`) compare; when exceeded, the LLM handler cancels the run and raises `Budget` with `data: { limit, consumed, allowed, scope }`; the overshoot is charged.
- `CapStack`: push/pop (`Scope { kind: Allow }`, task root from `allow`/`deny`, agent `allow`, call `allow`); provable-violation check at entry (same rules as WP-09 E0701) raising `Permission`; **effective permission = every scope on the stack permits** (intersection), so unprovable narrowing is still safe; `deny` subtracts and cannot be re-allowed.
- Enforcement wiring in the gateway/tool layer: `fs.*` via `path_permitted` for read/write; `shell`/`test` via `command_permitted`; `http.*`/MCP-over-HTTP via `host_permitted`; `gate`/`ask_human` require `human`; `parallel`/`race`/`map(concurrency>1)`/`spawn` require `spawn`; `env.get` requires the variable in `env`; `secrets.get` requires the name in `secrets`; violations raise `Permission` with `data: { cap, requested, scope_site }` and are journaled as effects with `status: "denied"`.
- Journal: `caps` (active effective set, rendered compactly) and `budget_after` on every entry; `scope.enter`/`scope.exit` journal entries carrying the spec and consumption (needed by `leash trace --budget`; add to `spec/journal.schema.json`).
- Post-hoc diff check after every `run` whose driver does not route tool calls through the runtime (CLI drivers), and optionally after all runs (`[runs] post_check = "always"`): diff against the pre-run snapshot; paths outside `fs.write` are restored from the snapshot (new files deleted), each journaled as a `permission.violation` entry; then fail with `Permission` unless `[runs] on_violation = "revert-and-continue"`.
- `DriverCapTranslation` and `translate(caps, budget) -> DriverCapTranslation` producing Claude Code flags (`--allowedTools` entries like `Bash(npm test*)`, `Read`, `Edit`, `Write`, `Grep`, `Glob`; `--disallowedTools WebFetch WebSearch` unless `net` non-empty; `--add-dir` for roots outside the workspace, which v1 never grants; `--permission-mode`; `--max-budget-usd` from remaining cost), Codex settings (`--sandbox read-only|workspace-write`, `--add-dir`), and a list of `unenforceable` items (e.g. subprocess network egress) that the runtime prints at run start as warnings.
- `leash trace <run> --budget`: tree of scopes from `scope.*` entries with limits, consumed and children.

## Out of scope
- Static checks (WP-09). Drivers themselves (WP-13/18/19 consume `DriverCapTranslation`). Gate policies and TTY prompting (WP-22 owns UX; this WP only enforces `human`).
- Human-time budgets and webhook gates (post-v1).

## Interfaces
```rust
// crate leash-runtime (module scope) and leash-effects (policy consumers)
// Money, Budget, CapSet and Usage are leash-spec types (WP-02); this WP adds the stacks and accounting only.
pub struct Budget { pub tokens: Option<u64>, pub cost: Option<Money>, pub wall: Option<Duration>, pub tool_calls: Option<u32>, pub llm_calls: Option<u32>, pub turns: Option<u32> }
pub struct Consumption { pub tokens: u64, pub cost: Money, pub tool_calls: u32, pub llm_calls: u32, pub turns: u32, pub wall: Duration }  // derived from leash-spec::Usage by the gateway
pub enum Counter { ToolCall, LlmCall, Turn }
pub struct BudgetStack { … }
impl BudgetStack { pub fn push(&mut self, site: SiteId, limits: Budget) -> Result<ScopeId, Failure>; pub fn pop(&mut self, id: ScopeId) -> ScopeSummary;
                   pub fn reserve(&mut self, c: Counter) -> Result<(), Failure>; pub fn charge(&mut self, u: &Usage) -> Result<(), Failure>;
                   pub fn remaining(&self) -> Budget; pub fn deadline(&self) -> Option<Instant>; pub fn snapshot(&self) -> BudgetAfter; }
pub struct CapSet { pub fs_read: Globs, pub fs_write: Globs, pub shell: Vec<ShellPattern>, pub net: Vec<HostPattern>, pub human: bool, pub spawn: bool, pub env: Vec<String>, pub secrets: Vec<String> }
impl CapSet { pub fn read_only() -> Self; pub fn provable_violation(&self, parent: &Self) -> Option<CapViolation>; pub fn minus(&self, deny: &Self) -> Self; pub fn render_compact(&self) -> serde_json::Value; }
pub struct CapStack { … }
impl CapStack { pub fn push(&mut self, site: SiteId, caps: CapSet) -> Result<ScopeId, Failure>; pub fn pop(&mut self, id: ScopeId);
                pub fn permits_path(&self, rel: &Path, access: Access) -> bool;      // all scopes
                pub fn permits_command(&self, cmd: &str) -> Result<Permit, Failure>;  // all scopes; Permit from leash-policy
                pub fn permits_host(&self, host: &str) -> bool; pub fn permits(&self, flag: CapFlag) -> bool; pub fn effective(&self) -> CapSet; }
pub struct DriverCapTranslation { pub claude_code: ClaudeCodeFlags, pub codex: CodexFlags, pub notes: Vec<String>, pub unenforceable: Vec<String> }
pub fn translate(caps: &CapSet, remaining: &Budget) -> DriverCapTranslation;
pub struct PostRunCheck; impl PostRunCheck { pub fn run(ws: &Workspace, caps: &CapSet, before: &SnapshotRef, journal: &mut Journal, policy: OnViolation) -> Result<Vec<Violation>, Failure>; }
```
`EffectCtx` (WP-04) gains `budget: &BudgetStack`, `caps: &CapStack`, `deadline: Option<Instant>`.

## Deliverables
- `crates/leash-runtime/src/scope/{budget,caps,pricing,translate,postcheck}.rs`; gateway changes; journal schema update; `leash trace --budget`.
- `std/prices.toml` populated for `claude-opus-5`, `claude-sonnet-5`, `claude-haiku-4-5`, `claude-fable-5-1` from `docs/research`/API reference values (cite the date in a comment).
- Tests: `tests/budget_scopes.rs`, `tests/budget_wall.rs`, `tests/budget_tokens.rs` (mock driver reporting usage), `tests/caps_stack.rs`, `tests/postcheck.rs`, `tests/translate.rs` (snapshot of flags for three CapSets), `tests/trace_budget.rs`.

## Acceptance criteria
- `cargo nextest run -p leash-runtime -p leash-effects` → green.
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
