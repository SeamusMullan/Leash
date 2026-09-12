# Leash Runtime and Compiler Architecture

Status: draft v0.1. Implementation language: Rust (see `docs/adr/0001-implementation-language.md`).

## 1. Overview

```
 .leash source ──► lexer ──► parser ──► AST ──► resolve ──► type+effect check ──► cap check
                                                                   │
                                                                   ▼
                                                      lowering ──► LIR (JSON-serialisable)
                                                                   │
                       ┌───────────────────────────────────────────┤
                       ▼                                           ▼
                 leash check / fmt / lsp                    leash run / replay / resume / fork
                                                                   │
                                                                   ▼
                                    ┌──────────────── Interpreter (tokio) ────────────────┐
                                    │  values · env · scopes (budget, caps) · scheduler   │
                                    │        effect boundary: EffectRequest               │
                                    └───────────────┬──────────────────────┬──────────────┘
                                                    ▼                      ▼
                                              Journal (JSONL)        Effect handlers
                                              + CAS blobs             ├─ llm  ─► Drivers (anthropic, claude_code, codex, managed, mock)
                                                    │                 ├─ tool ─► builtin tools · Leash tools · MCP client
                                                    ▼                 ├─ shell/fs ─► Sandbox
                                              Trace writers           ├─ human ─► Gate policy / TTY
                                              (8 traces, report)      └─ clock/rand/env
```

Three invariants hold the design together:

1. **Everything non-deterministic goes through an `EffectRequest`.** There is no
   other way for the interpreter to touch the world.
2. **The journal is the source of truth.** Traces, reports and metrics are
   derived from it. Replay consumes only the journal.
3. **The runtime is the authority on budgets and capabilities.** Drivers are
   asked to cooperate; the runtime verifies anyway.

## 2. Workspace layout (Rust)

```
Cargo.toml                 workspace
crates/
  leash-spec/              shared vocabulary: Money, Usage, Budget, CapSet, Effect, FailureKind, PayloadRef, profiles, price table, embedded JSON schemas
  leash-syntax/            lexer (logos), hand-written recursive-descent parser, AST, spans, diagnostics, formatter
  leash-types/             name resolution, type checker, effect inference, capability checker, JSON Schema gen
  leash-ir/                LIR data model (serde), AST→LIR lowering, LIR JSON (de)serialisation, hashing
  leash-runtime/           interpreter, values, Failure, scopes, scheduler, budget/capability stacks, journal, replay
  leash-store/             content-addressed store, run directory, manifest
  leash-policy/            shell tokenisation and command matching, path policy, host matching
  leash-effects/           EffectHandler traits, EffectCtx, builtin tools (fs, shell, git, test, http), sandbox
  leash-drivers/           Driver trait; anthropic (reqwest), claude_code, codex, managed_agents, mock; MCP (rmcp)
  leash-trace/             trace schemas, writers, report generator, outcome classification
  leash-std/               std library: Leash source (embedded) + native bindings
  leash-cli/               `leash` binary (clap)
  leash-lsp/               language server (tower-lsp)
  leash-eval/              harness-ladder runner, benchmark adapters, metrics
  leash-testkit/           fixtures, scripted mock drivers, journal assertions (dev-dependency)
spec/                      grammar.ebnf, lir.schema.json, journal.schema.json, traces/*.schema.json
std/                       Leash source of the standard library (embedded into leash-std at build time)
examples/                  example programs (also parser/type-check fixtures)
tests/                     integration tests, golden journals, mock scripts
```

Dependency direction: `spec ← syntax ← types ← ir ← runtime ← {effects, drivers, trace} ← cli/lsp/eval`,
with `leash-store` and `leash-policy` sitting beside `spec` as leaves. `leash-spec`
is the shared vocabulary crate every other crate may depend on; it keeps
`leash-drivers` and `leash-effects` independent of `leash-runtime`.
`docs/subagents/shared-interfaces.md` is normative for which crate owns which type.

## 3. Front end

### 3.1 Lexer
`logos`-based. Produces tokens with byte spans. String interpolation is lexed as
a token sequence (`StringStart`, `StringPart`, `InterpStart`, ..., `StringEnd`)
so the parser can parse embedded expressions. Newline tokens are emitted and the
parser applies the termination rule (spec §2.6).

### 3.2 Parser
Hand-written recursive descent with Pratt precedence for binary operators.
Error recovery at item and statement boundaries so `leash check` and the LSP
report multiple errors. Produces `ast::Program` with every node carrying a
`Span` and a stable `NodeId`.

### 3.3 Formatter
`leash fmt` pretty-prints the AST. Comments are preserved through attached
trivia. The formatter is idempotent and is tested by round-trip on `examples/`.

## 4. Semantic analysis

### 4.1 Name resolution
Builds a symbol table per module; resolves `use`, UFCS methods, enum variant
shorthand against expected types, contextual keywords.

### 4.2 Type checking
Bidirectional: `check(expr, expected)` and `infer(expr)`. Local inference only;
no global Hindley–Milner. Generics are instantiated at call sites by unification
of argument types. Record literals are coerced to nominal types by expected
type. Refinement predicates are type-checked as pure `Bool` expressions and
lowered into validator closures.

### 4.3 Effect inference
Compute a fixed point over the call graph: `effects(f) = ∪ effects(ops in body) ∪ effects(callees)`.
Declared clauses are upper bounds and are used for callers (so recursion and
separate compilation work). Lambdas carry their own set; higher-order std
functions are effect-polymorphic (`map: fn([T], fn(T)->U ! e) -> [U] ! e`).

### 4.4 Capability checking
For each `allow`/`budget` scope with literal sets, verify subset relations with
the enclosing scope. For each effectful operation with statically known
requirements (e.g. `shell("npm test")` literal), verify it is permitted by the
innermost literal `allow`. Non-literal cases produce an info diagnostic and are
enforced at run time.

### 4.5 Schema generation
`leash-types::schema` maps types to JSON Schema (spec §11) and produces a
canonical JSON string whose BLAKE3 hash identifies the schema.

## 5. LIR

A compact, typed, serialisable tree. Every node has: `id` (stable, derived from
module path + item + position), `span`, `ty`, `effects`. Effectful nodes
additionally have a `site_id` used for journal keys. LIR is emitted by `leash
ir file.leash` and consumed by `leash run --ir`. It is versioned
(`lir_version`) and has a JSON Schema in `spec/lir.schema.json`.

Why an IR at all: (a) tooling and meta-agents can consume programs without a
parser; (b) journal keys must be stable across formatting changes; (c) the
interpreter is simpler over a desugared tree (trailing lambdas, UFCS, `if`
chains, string interpolation and `for` loops are desugared).

## 6. Interpreter

### 6.1 Values
```rust
enum Value { Unit, Bool, Int(i64), Float(f64), Str(Arc<str>), Bytes(Arc<[u8]>), Duration, Money,
             List(Arc<[Value]>), Tuple(Arc<[Value]>), Record(Arc<Record>), Enum(Arc<EnumVal>),
             Map(Arc<BTreeMap<Key, Value>>), Fn(Arc<Closure>), Opaque(Arc<dyn Opaque>) , Null }
```
Immutable, persistent-by-copy values. `Opaque` covers runtime types (`Agent`,
`Repo`, `Handle`, ...). All values are serialisable to JSON for journaling
except closures and handles (which are never journaled: they cannot cross an
effect boundary).

### 6.2 Execution
A tree-walking evaluator over LIR, written as `async fn eval(&mut self, node) -> Result<Value, Failure>`
on tokio. Pure nodes evaluate synchronously inside the async fn. Effectful nodes
build an `EffectRequest { site_id, kind, input: Json, caps_required, budget_cost_estimate }`
and hand it to the **effect gateway** (§7). Control flow (`fail`, `break`,
`return`, `complete`) is modelled with a `Control` enum in the error channel.

### 6.3 Scopes
The interpreter keeps a scope stack. Each frame holds bindings plus optional
`BudgetScope` and `CapScope`. Entering `budget {...} {}` / `allow {...} {}` /
an `ask`/`run` call pushes narrowed scopes; exiting pops them and charges
consumption upwards.

### 6.4 Scheduler
`parallel`, `race`, `map(concurrency:)` and `spawn` create child tasks with a
copy of the enclosing immutable environment and their own **effect path prefix**
(branch index). Structured concurrency: a `JoinSet` per block; on block exit all
children are joined or cancelled. Cancellation propagates through the effect
gateway to drivers and subprocesses.

### 6.5 State and checkpoints
`state` is a record in the flow frame. `checkpoint "name"` (a) journals a
checkpoint entry containing the state snapshot and the budget consumption; (b)
asks the sandbox to snapshot the working copy (git commit to
`refs/leash/<run>/<checkpoint>` in the run worktree). `fork --at name` restores
both.

## 7. Effect gateway and journal

```
EffectRequest ──► budget check ──► capability check ──► journal lookup
                                                            │ hit (replay/resume): return recorded result
                                                            │ miss:
                                                            ▼
                                                      handler.perform() ──► journal append ──► trace fan-out
```

- **Journal**: append-only JSONL at `.leash/runs/<id>/journal.jsonl`. Entries
  are fsync'd before the result is returned to the program (crash safety).
  Large payloads (transcripts, file contents, diffs) are stored in a
  content-addressed store `.leash/cas/<blake3>` and referenced by hash.
- **Effect identity**: `effect_id = blake3(effect_path)` where `effect_path` is
  the sequence of `(site_id, occurrence)` pairs from the flow root plus branch
  indices. Full algorithm in `docs/04-determinism-and-replay.md`.
- **Secrets**: values obtained through `secrets.get` are tagged; the journal
  writer redacts them by hash-preserving substitution.

## 8. Effect handlers

```rust
#[async_trait]
trait EffectHandler {
    fn kind(&self) -> EffectKind;
    async fn perform(&self, req: EffectRequest, ctx: &EffectCtx) -> Result<EffectResult, Failure>;
    async fn cancel(&self, req_id: EffectId);
}
```
Handlers: `LlmHandler` (dispatches to a `Driver`), `ToolHandler` (builtin,
Leash-defined, MCP), `ShellHandler`, `FsHandler`, `HumanHandler`,
`ClockHandler`, `RandHandler`, `EnvHandler`. `EffectCtx` exposes the active `CapSet`, `BudgetScope`, the sandbox, the run
directory and a `TraceSink`. The LLM handler narrows it to a `DriverCtx` before
calling a driver (`docs/subagents/shared-interfaces.md`).

### 8.1 Tool layer
A `Tool` is `{ name, description, schema, caps_required, impl }` where `impl` is
`Builtin(fn)`, `Leash(closure)` or `Mcp(server, name)`. For API/mock drivers the
runtime executes tool calls itself in the model loop; for CLI drivers, Leash
tools and MCP tools are exposed to the CLI through a per-run MCP server started
by the runtime (`leash mcp-serve --run <id>`), so custom tools work uniformly
across drivers.

### 8.2 Sandbox
Per run: a working copy of the target repository (git worktree by default,
plain copy fallback), a scrubbed environment, a path policy derived from
`CapSet`, a shell policy (tokenised argv matching; no shell unless allowlisted),
timeouts, output caps. Backend trait allows a container backend (podman/docker)
later; v1 ships the process backend.

## 9. Drivers

```rust
#[async_trait]
trait Driver {
    fn id(&self) -> DriverId;                       // includes model + version + config hash
    async fn ask(&self, req: AskRequest, ctx: &DriverCtx) -> Result<AskResponse, DriverError>;
    async fn run(&self, req: RunRequest, ctx: &DriverCtx, events: EventSink) -> Result<RunResponse, DriverError>;
    fn capabilities(&self) -> DriverCaps;           // structured output? tools? effort? sessions?
}
```
- `anthropic`: raw HTTPS to `POST /v1/messages` with structured outputs
  (`output_config.format`), strict tools, adaptive thinking, effort, prompt
  caching breakpoints on the stable prefix; the runtime owns the tool loop.
- `claude_code`: spawns `claude -p --output-format stream-json --json-schema …
  --allowedTools … --max-budget-usd … --permission-mode … --mcp-config …`;
  parses events into tool-trace entries and usage.
- `codex`: spawns `codex exec --json --output-schema … --sandbox … --cd …`.
- `managed_agents`: Anthropic Managed Agents sessions (post-v1).
- `mock`: scripted responses keyed by prompt hash or sequence; used by
  `leash test` and CI.
Full driver contracts: `docs/09-drivers-and-tools.md`.

## 10. Trace layer

Trace writers subscribe to journal entries and produce the eight trace files
(action, tool, context, verification, attribution, intervention, entropy,
outcome), the verification report (Markdown) and the outcome label. Because
they are derived, `leash report <run>` can regenerate them from the journal at
any time, including with a newer runtime.

## 11. CLI and LSP

The CLI is a thin layer: parse args → load package → run pipeline → print.
The LSP reuses `leash-syntax` and `leash-types` for diagnostics, hover (types
and effects), go-to-definition and formatting.

## 12. Cross-cutting concerns

- **Errors**: `thiserror` types per crate; `Failure` is the only user-visible
  error type at run time; diagnostics with spans for compile time (`miette`).
- **Logging**: `tracing` with a `--log` flag; never mixes with the journal.
- **Determinism tests**: every integration test runs the program twice — live
  against the mock driver, then `replay` — and asserts identical outcome and
  identical derived traces.
- **Performance**: the interpreter is not the bottleneck; model calls are.
  Journal writes are sequential and small; CAS handles blobs. Target: replay of
  a 10k-effect journal in under 2 s.
- **Security**: see `docs/07-budgets-and-capabilities.md` §6 for the threat model.
