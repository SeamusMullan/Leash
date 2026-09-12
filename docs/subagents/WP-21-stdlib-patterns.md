# WP-21: Standard library and patterns, roles, caps presets, entropy audit

Milestone: M7 · Size: L · Status: see `docs/status.md`

## Goal
The `std` package exists: every name listed in `docs/02` §12 resolves, is typed and works; the multi-agent patterns in `docs/08` §3 are implemented in Leash source with explicit convergence criteria and tests; the entropy audit, capability presets and role presets ship; and native functions are bound through one registry so that `leash-types` (signatures) and `leash-runtime` (implementations) agree by construction.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/02-language-spec.md` §4.4 (UFCS), §5.5 (built-in effects), §6 (agents), §12 (stdlib surface)
- `docs/05-harness-model.md` §2 (caps presets, tool hooks), §4 (failure kinds)
- `docs/06-verification-and-evidence.md` §2 (check methods), §7 (report template), §10 (entropy heuristics)
- `docs/08-multi-agent.md` §2–§4
- `docs/09-drivers-and-tools.md` §7 (tools), §8 (prices)
- `docs/10-cli-and-tooling.md` §6 (`test` declarations)
- `docs/03-runtime-architecture.md` §2 (`leash-std`, `std/`)

## Scope
- **Native registry.** `crates/leash-std` defines `NativeFn { name, signature, effects, f }` registered with `inventory::submit!` through a `#[leash_native(name = "std.core.len", sig = "fn len<T>(xs: [T]) -> Int")]` attribute macro (crate `leash-std-macros`). `signature` is Leash type syntax parsed by `leash-syntax` at startup; a mismatch between signature arity and the Rust implementation is a startup panic in debug builds and a unit test in CI. `leash_std::signatures()` feeds the type checker (WP-08 provides `TypeEnv::register_native`); `leash_std::natives()` feeds the interpreter's call dispatch (WP-04 provides `Interpreter::register_native`).
- **Leash sources** under `std/`, embedded at build time (`include_dir!`) and exposed as `leash_std::sources()`; the loader compiles them into the implicit `std` package before user code.
- `std.core`: everything in `docs/02` §12 (`len range min max abs to_string parse_int parse_json to_json hash usd`; list, string and option methods) as natives with UFCS-visible signatures. Method resolution for built-in types goes through the same signature table (`fn contains(s: String, needle: String) -> Bool` is callable as `s.contains(needle)`).
- `std.drivers`: `anthropic`, `claude_code`, `codex`, `managed_agents` (constructor only), `mock` constructors returning opaque `Driver` values, with typed option enums: `Effort { Low, Medium, High, XHigh, Max }`, `PermissionMode { AcceptEdits, DontAsk, Plan, BypassPermissions }`, `Sandbox { ReadOnly, WorkspaceWrite, DangerFullAccess }`.
- `std.tools`: bindings for `fs.*`, `shell`, `test`, `git.*`, `http.*`, `lint` as `Tool` values (implementations in WP-11); `Tool.before(fn)` / `Tool.after(fn)` hooks (`docs/05` §2).
- `std.repo`, `std.context`, `std.verify`, `std.failure`, `std.mcp`, `std.human` (`gate`, `ask_human`, consulting the gate policy from `EffectCtx`), `std.trace`, `std.time`, `std.env`, `std.runs` (`load(run_id) -> EpisodePackage` with typed access to outcome, traces and journal summaries), `std.artifacts` (`save(name, content)` into `artifacts/`).
- `std.audit.entropy(repo, since: String) -> EntropyReport` implementing every heuristic in `docs/06` §10 (unreferenced new files, deleted or skipped tests, removed assertions, manifest changes, debug prints, TODO/FIXME, docs referencing removed symbols) with `max_severity` and `items`.
- `std.caps`: `read_only`, `sandbox_edit(write_globs, shell:)`, `full_access` presets (`docs/05` §2).
- `std.patterns.roles`: `planner coder reviewer tester attributor judge manager` as `Agent` templates parameterised by driver.
- `std.patterns`: `fix_verify_report`, `review`, `critique_repair`, `debate`, `self_consistency`, `plan_execute`, `triage`, `tournament`, `independent_tests`. Each is a `pub fn` in `std/patterns/<name>.leash`, ≤ ~60 lines, with the convergence criterion from `docs/08` §3 stated in a `Converges:` doc line and enforced in code (no fixed-iteration loop without a `Recovery` failure at exhaustion), and at least one `test "…" with { drivers: … }` block using `drivers.mock`.
- `std/templates/report.md` (the template from `docs/06` §7, placeholders documented at the top) and the consumer of `std/prices.toml` (`std.prices.cost(model, usage) -> Money`, overridable by `leash.toml`).
- Doc comments on every public item (consumed by `leash doc`, WP-24).

## Out of scope
- Tool *implementations* (WP-11), driver implementations (WP-05/13/18/19), the `select` renderer (WP-17), the `complete`/evidence machinery (WP-15): this WP binds and documents them.
- Any pattern that needs shared mutable state (blackboard) — M11.
- Currencies other than USD.

## Interfaces
```rust
// crates/leash-std/src/lib.rs
pub struct NativeFn { pub name: &'static str, pub signature: &'static str, pub effects: &'static [Effect], pub f: NativeImpl }
pub type NativeImpl = for<'a> fn(&'a mut EffectCtx, Vec<Value>) -> BoxFuture<'a, Result<Value, Failure>>;
inventory::collect!(NativeFn);
pub fn natives() -> impl Iterator<Item = &'static NativeFn>;
pub fn signatures() -> &'static [ParsedSignature];          // parsed once, cached
pub fn sources() -> &'static [(&'static str, &'static str)]; // ("std/core.leash", "...")
```
```leash
// std/patterns/fix_verify_report.leash (shape; body ≤ 60 lines)
/// Reproduce → attribute → fix → verify → report.
/// Converges: `verify ensure` passes; fails with Recovery after `max_attempts` verified failures,
/// carrying the last Attribution in `data`.
pub fn fix_verify_report(coder: Agent, attributor: Agent, repo: Repo, issue: String,
                         reproduce: fn() -> ShellResult ! {shell}, max_attempts: Int = 3)
  -> Patch ! {llm, tool, shell, fs.write}

/// Converges: policy over `Review.blocking` (.AllApprove | .Majority).
pub fn review(patch: Patch, reviewers: [Agent], policy: ReviewPolicy = .AllApprove) -> [Review] ! {llm, spawn}
pub fn critique_repair<T>(coder: Agent, critic: Agent, artifact: T, until: fn(T) -> Bool, max_rounds: Int) -> T ! {llm}
pub fn debate(a: Agent, b: Agent, topic: String, rounds: Int) -> Transcript ! {llm}
pub fn self_consistency<T>(agent: Agent, prompt: String, n: Int, pick: fn([T]) -> T) -> T ! {llm, spawn}
pub fn plan_execute<T>(planner: Agent, workers: [Agent], task: String, verify_step: fn(Step) -> Bool ! {shell}) -> [T] ! {llm, spawn, shell}
pub fn triage<T, U>(agent: Agent, items: [T], render: fn(T) -> String, concurrency: Int = 8) -> [U] ! {llm, spawn}
pub fn tournament<T>(candidates: [T], judge: Agent, render: fn(T, T) -> String) -> T ! {llm}
pub fn independent_tests(tester: Agent, spec: String, repo: Repo) -> [Check] ! {llm, fs.write}
```
Shared enums: `Backoff { None, Fixed(d: Duration), Exponential(base: Duration) }`, `OnTimeout { Fail, Record, Skip }`, `ReviewPolicy { AllApprove, Majority }`, `GatePolicy { Tty, Deny, Approve, Fail }`. Effect variables in native signatures are written `! e` and unions `! e + {spawn}`; the syntax is agreed with WP-08.

## Deliverables
- `crates/leash-std` and `crates/leash-std-macros` with the registry and embedded sources.
- `std/` tree: `core.leash drivers.leash tools.leash repo.leash context.leash verify.leash failure.leash mcp.leash human.leash trace.leash time.leash env.leash runs.leash artifacts.leash audit.leash caps.leash prices.leash patterns/*.leash patterns/roles.leash templates/report.md`.
- `std/prices.toml` entries for every model in `docs/09` §8 (WP-02's file if present; otherwise created here).
- Tests: unit tests for every native (`crates/leash-std/tests/natives.rs`), `test` declarations in every pattern file, entropy fixtures under `tests/fixtures/entropy/` (one per heuristic), a signature/implementation arity conformance test.
- `docs/02` §12 updated to the final signatures; `docs/generated/stdlib.md` snapshot until WP-24's `leash doc` replaces it.

## Acceptance criteria
- `leash check examples/*.leash` resolves every `std` name with no "unknown identifier" diagnostic.
- `leash test std/` runs every pattern test with the mock driver and passes; a second `leash test std/ --reuse` performs zero live mock calls.
- `wc -l std/patterns/*.leash` shows no file above 80 lines (pattern ≤ ~60 plus its tests).
- `cargo nextest run -p leash-std` passes, including `natives_signature_arity` and one test per entropy heuristic (each fixture yields exactly its expected `category`/`severity`).
- `retry(3, on: Tool, backoff: .Exponential(1s)) { fail Tool("x") }` with the mock driver journals two `clock` entries requesting 1 s and 2 s (no sleep after the last attempt) and fails with kind `Tool` whose `cause` chain is two deep.
- `std.audit.entropy` on a fixture that deletes a test and adds an unreferenced `debug.js` reports two items with severities 3 and 2.
- A flow using `allow caps.sandbox_edit(["src/**"], shell: ["npm test*"])` is denied `fs.write("README.md", …)` with `Permission`.
- `cargo clippy --all-targets -- -D warnings` and `cargo fmt --check` clean.

## Notes and pitfalls
- Signatures live in Rust attributes because the grammar has no `extern` declaration; if WP-07 adds one, migrate. Record this as a follow-up in `docs/status.md` either way.
- Patterns must stay boring. If a pattern needs more than ~60 lines, split it or move it to `examples/`.
- Every `pub` item declares its effects. This is mandatory, not aspirational:
  `std` is the one tree where `--strict` is required (`docs/02` §5.3), and
  `leash check --strict std/` runs in CI.
- `gate` must consult the run's gate policy through `EffectCtx`, never the TTY directly; the policy is owned by WP-22.
- Entropy heuristics are language-aware only for the runners listed in `docs/06` §2; unknown languages produce no false positives.
- Keep `usd()` exact: parse decimal strings, never `f64`.
- `std.runs.load` reads only the run directory (`fs.read` effect) and never re-executes anything.

## Dependencies
Hard: WP-15 (checks/evidence/complete used by patterns), WP-16 (failures, retry), WP-20 (spawn/map for parallel patterns).  Soft: WP-08 (native signature hook), WP-11 (tool implementations), WP-17 (`select`), WP-24 (`leash doc`).

## Estimated size
L (1–2 weeks).
