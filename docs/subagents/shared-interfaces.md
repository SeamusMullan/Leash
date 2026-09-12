# Shared Interfaces: who owns which type

Status: normative for work-package briefs. Several briefs were written in
parallel and sketched the same type twice; this file decides the single owner
and the canonical shape. If a brief disagrees with this file, this file wins;
fix the brief in the PR that notices it.

Rule of thumb: a type is defined in the lowest crate that needs it and
re-exported upward. Crates never define a second copy "of the same shape".

## Crate ownership

| Crate | Owns | Notes |
|---|---|---|
| `leash-spec` | `Money`, `Usage`, `PayloadRef`, `Effect`, `EffectSet`, `Budget`, `CapSet`, `Globs`, `ShellPattern`, `HostPattern`, `FailureKind`, `Profile`, price table, and the embedded JSON schemas | WP-02. Dependency-free except `serde`, `globset`, `shell-words`, `jsonschema`. Every other crate depends on it. |
| `leash-syntax` | `Span`, `NodeId`, `Diagnostic`, `Severity`, AST, lexer, parser, formatter | WP-03/07 |
| `leash-types` | `Ty`, `TypeId`, `TypedProgram`, `EffectAnalysis`, `CapSetLit`, `HoverInfo`, JSON Schema generation | WP-08/09 |
| `leash-ir` | `Lir`, `LirNode`, `SiteId`, lowering | WP-10 |
| `leash-runtime` | `Value`, `Failure`, `EffectRequest`, `EffectResult`, `EffectGateway`, `Journal`, `BudgetStack`, `CapStack`, `Scheduler`, `Handle` | WP-04/12/16/20 |
| `leash-store` | content-addressed store, run directory, manifest | WP-06 |
| `leash-effects` | `EffectHandler`, `EffectCtx`, sandbox, built-in tools, MCP client, tool middleware | WP-11/14 |
| `leash-policy` | shell tokenisation and command matching, path policy, host matching (`Permit`) | WP-11/12; split out so `leash-types` can reuse matching without depending on the runtime |
| `leash-drivers` | `Driver`, `DriverCtx`, `DriverCaps`, `AskRequest/Response`, `RunRequest/Response`, `StopReason`, `DriverError`, `EventSink`, `DriverEvent`, subprocess helpers | WP-05/13/18/19 |
| `leash-trace` | trace writers, `Evidence` serialisation, report, outcome classification | WP-06/15 |
| `leash-testkit` | fixtures, scripted mocks, journal assertions | WP-25 |

`leash-spec` and `leash-store` are created empty by WP-01. `leash-policy` is created by whichever of WP-09 and WP-11 lands first (both
need command and path matching) and consumed by WP-12.
`docs/03-runtime-architecture.md` §2 lists the same set.

## Canonical shapes

```rust
// leash-spec
pub struct Money(i64);                 // exact micro-USD; i64 covers ±9.2e12 USD
impl Money { pub fn usd(v: f64) -> Self; pub fn micros(v: i64) -> Self; pub fn checked_sub(self, o: Self) -> Option<Self>;
             pub fn display(&self) -> String; }   // "$3.1200"

pub struct Usage {                     // what a driver reports for one turn or one call
    pub input_tokens: u64, pub output_tokens: u64,
    pub cache_read_input_tokens: u64, pub cache_creation_input_tokens: u64,
    pub cost: Option<Money>,           // Some when the backend reports cost (Claude Code `total_cost_usd`)
    pub model: String,
}
impl Usage { pub fn total_tokens(&self) -> u64; }

pub enum PayloadRef { Inline(serde_json::Value), Cas(String) }   // Cas holds "b3:<hex>"

pub struct Budget { pub tokens: Option<u64>, pub cost: Option<Money>, pub wall: Option<Duration>,
                    pub tool_calls: Option<u32>, pub llm_calls: Option<u32>, pub turns: Option<u32> }

pub struct CapSet { pub fs_read: Globs, pub fs_write: Globs, pub shell: Vec<ShellPattern>,
                    pub net: Vec<HostPattern>, pub human: bool, pub spawn: bool,
                    pub env: Vec<String>, pub secrets: Vec<String> }
```

Counters that are *consumed* rather than *reported* (`tool_calls`, `llm_calls`,
`turns`) live in `BudgetStack` accounting, not in `Usage`. A driver reports
`Usage`; the runtime converts it into budget consumption.

```rust
// leash-syntax
pub struct Diagnostic { pub code: &'static str, pub severity: Severity, pub message: String,
                        pub labels: Vec<(Span, String)>, pub help: Option<String> }
```
Single-span diagnostics use one label. `leash-types` and `leash-lsp` reuse this
type; neither defines its own.

```rust
// leash-runtime
pub struct Failure { pub kind: FailureKind, pub message: String, pub data: Option<serde_json::Value>,
                     pub site: Site, pub cause: Option<Box<Failure>> }
```
`FailureKind` (the eleven kinds of `docs/02-language-spec.md` §9.1) lives in
`leash-spec` so drivers, effects and the trace layer can name kinds without
depending on the runtime. `Failure` itself lives in `leash-runtime`; lower
crates return their own error enums (`DriverError`, `ToolError`, `SpecError`)
and the runtime maps them to kinds at the effect gateway (`docs/02` §9.6).

## Context objects

- `EffectCtx` (`leash-effects`) is what an **effect handler** gets: active
  `CapStack` view, `BudgetStack` view, sandbox handle, run directory, trace
  sink, cancellation token.
- `DriverCtx` (`leash-drivers`) is what a **driver** gets: `run_id`,
  cancellation token, payload sink, price table and the translated
  `DriverCapTranslation`. Drivers never see the runtime's stacks. The LLM
  effect handler builds a `DriverCtx` from its `EffectCtx`.

`docs/09-drivers-and-tools.md` §1 uses `DriverCtx` for this reason.

## Naming collisions to avoid

- `Workspace` means the LSP's analysed package set (`leash-lsp`). The sandbox's
  per-run working copy is `RunWorkspace` (`leash-effects`), never `Workspace`.
- `Context` is the Leash value produced by `select` (`leash-runtime`). Rust
  context objects are always `*Ctx`.
- `Check` is the task-level verification check (`leash-spec` + `leash-trace`).
  `leash check` the command is unrelated; do not name a Rust type `Check` in
  the CLI crate.
