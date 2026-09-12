# Leash Language Specification

Status: draft v0.1 (2026-09-12). Normative where it says MUST/MUST NOT. The
grammar in `spec/grammar.ebnf` is normative for syntax. When this document and
the grammar disagree, file an issue; the grammar wins for syntax, this document
wins for semantics.

Contents

1. Source, modules, items
2. Lexical structure
3. Types
4. Expressions and statements
5. Effects and purity
6. Agents, ask, run, spawn
7. Tasks, flows, checks, evidence
8. Budgets and capabilities
9. Failures and recovery
10. Concurrency
11. Type ↔ JSON Schema mapping
12. Standard library surface (v1)
13. Open questions

---

## 1. Source, modules, items

- Source files use the `.leash` extension and are UTF-8.
- A file MAY start with `module a.b.c`. Without it, the module name is derived
  from the file path relative to the package root (`examples/fix_bug.leash` →
  `examples.fix_bug`).
- A package is a directory with a `leash.toml` manifest (name, version,
  dependencies, driver defaults, profile). The standard library is the implicit
  package `std`.
- `use std.repo.{Repo, Patch}` imports names. `use std.drivers` imports the module
  as a namespace. There are no wildcard imports.
- Items: `let` (constants), `type`, `enum`, `fn`, `tool`, `agent`, `memory`,
  `task`, `flow`, `test`. Items are visible package-wide; `pub` exports them.
- Item order does not matter. Cycles between items are allowed for functions and
  types, forbidden for constants.
- Module-level `let` MUST be pure (effect set `{}`). Descriptors such as
  `file("prompts/x.md")` and `mcp.connect(...)` are pure: they describe a resource;
  reading or connecting happens later, at the effectful use site.

## 2. Lexical structure

2.1 Identifiers: `[A-Za-z_][A-Za-z0-9_]*`. Types, enums, tasks and flows are
PascalCase by convention; values and functions snake_case; enum variants
PascalCase.

2.2 Hard keywords (reserved): `module use pub let var fn type enum tool agent
memory task flow ask run spawn await parallel race retry attempt recover
fail checkpoint verify complete if else match for in while loop break continue
return true false null and or not as where budget allow`.

2.3 Contextual keywords: `test` (only as an item: `test "name" {`), `input
output require ensure deny check by expect within on_timeout state on backoff
concurrency limit with`. They are ordinary identifiers outside the positions
the grammar names, so `test("npm test")` inside `by` is the std function.

2.4 Literals
- Int: 64-bit signed. `1_000_000`. Overflow is a runtime failure of kind
  `Unknown` with message "integer overflow".
- Float: 64-bit IEEE. `0.25`, `1e6`.
- Duration: `500ms`, `30s`, `5m`, `2h`. Type `Duration`.
- String: `"..."` with escapes and `{expr}` interpolation. `"""` multi-line
  strings start after the opening line break; the common leading indentation of
  all non-blank lines is stripped; the final line break before `"""` is removed.
  `r"..."` / `r"""..."""` are raw: no escapes, no interpolation.
- `true`, `false`, `null`.

2.5 Comments: `//` line, `/* */` nesting block, `///` doc comments attach to the
next item and are part of the program (they become tool descriptions and appear
in LIR).

2.6 Statement termination. A statement ends at a newline unless the next token
is one that cannot start a statement and can continue an expression: a binary
operator, `.`, `?`, `(` on the same line as the previous token, `,`, `)`, `]`,
`}`, `=>`, `else`, `recover`. `;` is allowed to separate statements on one line.

## 3. Types

3.1 Primitive: `Int`, `Float`, `Bool`, `String`, `Bytes`, `Duration`, `Money`,
`Unit`, `Json`.

- `Money` is a decimal amount with a currency; v1 supports `USD` only via
  `usd(x)`. Arithmetic between `Money` values is exact (fixed-point, 6 places).
- `Json` is an opaque JSON value with `.get("k")`, `.as_string()`, etc. It exists
  so that MCP tool results with unknown schemas can be handled.

3.2 Composite: optional `T?`, list `[T]`, tuple `(A, B, ...)`, record
`{ a: A, b: B }`, `Map<K, V>` (K is `String` or `Int`), function
`fn(A, B) -> C ! {e}`, generic named types `Handle<T>`.

3.3 Nominal types: `type Name = <type>` creates a distinct nominal type when the
right-hand side is a record; otherwise it is an alias. `enum` declares a nominal
sum type. Variants may carry named fields.

3.4 Records are structural for literals: `{ id: 1 }` has type `{ id: Int }`.
A literal is coerced to a nominal record type when the expected type is known
and the fields match exactly (no missing, no extra). `Name { ... }` constructs a
nominal record explicitly.

3.5 Refinements: `field: T where <predicate>`. The predicate is a pure Bool
expression over the field name and other fields of the same record. Refinements
are checked:
- when a value of the type is constructed from a literal (compile-time if
  constant, runtime otherwise);
- when a value enters the program from an effect (`ask`, `run`, tool results,
  `--input`).
A violated refinement in an effect result triggers the effect's validation-retry
policy; if retries are exhausted, `fail Model(...)` for model outputs and
`fail Tool(...)` for tool outputs.

3.6 Subtyping. There is no subtyping except: `T` is assignable to `T?`; a record
literal to a nominal record; an enum variant literal `.X` to its enum when the
expected type is known. Generic functions are parametric.

3.7 Inference. Local `let` bindings are inferred. Function parameters MUST be
annotated; return types MAY be omitted for non-`pub` functions. Bidirectional
checking propagates expected types into `ask`, `run`, record and variant
literals, and lambdas.

3.8 Runtime-provided types (defined in `std`, opaque to user code):
`Agent`, `Driver`, `Tool`, `Resource`, `Repo`, `Patch`, `Context`, `Check`,
`Evidence`, `Budget`, `CapSet`, `Failure`, `FailureKind`, `Handle<T>`,
`McpServer`, `RunReport`, `ShellResult`, `TestResult`, `Usage`.

## 4. Expressions and statements

4.1 Everything is an expression. `Block`, `if`, `match`, `attempt`, `retry`,
`parallel`, `race`, `ask`, `run` yield values. A block without a trailing
expression yields `Unit`.

4.2 `let` binds immutably and supports destructuring patterns. `var` binds a
mutable local. Assignment targets are `var` locals, `state` fields and
elements/fields reachable from them. Records and lists are values (copy
semantics); mutation of a `var` record replaces it.

4.3 Operators: `and or not` (short-circuit), `== != < <= > >=`, `+ - * / %`,
unary `-`. `+` concatenates strings and lists. `==` is structural equality on all
types except functions, handles and opaque runtime types (identity).

4.4 Calls. Positional and named arguments (`f(1, name: "x")`). Named arguments
match parameter names; defaults fill omitted parameters. A trailing lambda
`f(args) { x => body }` is passed as the last positional argument. UFCS:
`x.f(y)` means `f(x, y)` if `f` is in scope and its first parameter accepts `x`;
built-in types have methods defined in `std`.

4.5 `match` MUST be exhaustive. Patterns: literals, bindings, `_`, tuples,
lists with `..rest`, records, enum variants `.V(p)`, failure kinds `Verify(msg)`.

4.6 Loops: `for p in xs {}`, `while c {}`, `loop {}` with `break value`.
Iteration order over lists is index order; over `Map`, key order (sorted). This
is required for determinism.

4.7 `checkpoint "name"` is a statement. Names MUST be unique within a flow at
runtime (a loop may not execute the same checkpoint twice; use interpolation to
make names unique, e.g. `checkpoint "attempt-{i}"`).

## 5. Effects and purity

5.1 Effect set: `llm`, `tool`, `shell`, `fs.read`, `fs.write`, `net`, `spawn`,
`human`, `clock`, `rand`, `env`. `fs.write` implies `fs.read`.

5.2 Every function, tool, flow and lambda has an effect set. It is the union of
the effects of the operations it performs and the declared effects of the
functions it calls. Inference is a fixed-point over the call graph.

5.3 A declared `EffectClause` is an upper bound: the body's inferred effects MUST
be a subset, and the declared set is what callers see.

`leash check --strict` requires every `pub` function, `pub tool` and `flow` to
declare its effects. The flag is **off by default** for user packages, because
the ceremony is not worth it while a program is being written and its effects
are still moving. It is **mandatory for the standard library**: `std` must be
strict-clean, and CI enforces that.

The reason for the split is that inference derives the effect set from the body,
so a `pub` item's effects can widen silently when someone edits its
implementation, and callers get no signal. Where code is depended on across a
package boundary the effect clause should be a contract that changes visibly in
review, not a summary regenerated on each build. Inside a package, inference is
enough. This mirrors the usual rule that public signatures are declared while
local types are inferred.

The default is expected to flip to on before 1.0, once the standard library has
settled. Packages that want the stricter rule now can set `strict = true` under
`[check]` in `leash.toml`.

5.4 Purity guarantees. A function with effect set `{}`:
- MUST NOT observe time, randomness, environment, files, network or models;
- is memoisable and re-executable during replay without side effects;
- may allocate and compute without bound (budgets bound wall time at the flow
  level, not inside pure code).

5.5 Built-in operations and their effects:
| Operation | Effect |
|---|---|
| `ask`, `run` | `llm` (plus `tool` when the run has tools) |
| calling a `Tool` value | `tool` plus the tool's own effects |
| `shell(...)`, `test(...)` | `shell` |
| `fs.read/glob/grep` | `fs.read` |
| `fs.write/edit/mkdir/rm` | `fs.write` |
| `http.*`, MCP over HTTP | `net` |
| `spawn`, `parallel`, `race`, `map(concurrency>1)` | `spawn` |
| `gate` | `human` |
| `clock.now()`, `sleep` | `clock` |
| `random()` | `rand` |
| `env.get`, `secrets.get` | `env` |

5.6 Every effectful operation is a **journal point**. See
`docs/04-determinism-and-replay.md`.

## 6. Agents, ask, run, spawn

6.1 `Agent { driver, system, tools?, budget?, allow?, options? }`.
- `driver: Driver` — constructed by `std.drivers.*` functions.
- `system: String | Resource` — a `Resource` is read at run time; its content hash
  is journaled.
- `tools: [Tool]` — default tool set for `run`.
- `budget`, `allow` — defaults applied per call, further narrowed by the call site.

6.2 `ask agent(prompt, opts...) -> T`
- `prompt: String`. Named options: `retries: Int = 2`, `effort`, `model`,
  `temperature: Float?` (ignored by drivers that do not support it), `system:
  String?` (appended), `context: Context?`, `attachments: [Resource]?`.
- The expected type `T` is taken from the annotation or the surrounding expected
  type. Without one, `T = String`.
- If `T != String`, the driver is instructed to produce JSON matching the schema
  of `T` (§11). The runtime parses, validates, applies refinements. On failure it
  re-asks with the validation error appended, up to `retries` times, each attempt
  journaled. Then `fail Model`.
- `ask` is a single model turn. It MUST NOT call tools. Drivers that only support
  agentic sessions (Claude Code, Codex) run `ask` with tools disabled and
  `max_turns = 1`.

6.3 `run agent(task, opts...) -> T`
- `task: String`. Named options: `context: Context?`, `tools: [Tool]?`
  (subset of the agent's tools; if omitted, the agent's tools), `budget`,
  `allow`, `max_turns: Int?`, `effort`, `model`, `retries: Int = 1`,
  `workdir: Repo?`.
- The driver runs an agentic session. Tool calls made by the model are executed
  by the runtime's tool layer (for API and mock drivers) or by the CLI's own
  loop (Claude Code, Codex) under a capability set derived from `allow`.
- `T` defaults to `RunReport { summary: String, files_changed: [String], usage: Usage }`.
  With a different expected type the driver is asked for a final structured
  answer matching the schema.
- Every tool call inside a run is journaled as a child effect of the run and
  written to the tool trace.

6.4 `spawn <ask|run|call>` returns `Handle<T>` and adds `spawn` to the effect set.
`await h` returns `T` or re-raises the branch's failure. A handle not awaited by
the end of its enclosing block is awaited there (join) unless the block exits by
failure, in which case it is cancelled. Cancellation is best-effort for
subprocess drivers (SIGTERM, then SIGKILL after a grace period) and recorded.

6.5 Agents and tools passed to drivers are serialised into driver-specific
configuration. A driver that cannot honour an option (for example `temperature`
on Claude Code) MUST record the omission in the journal entry and MUST NOT fail.

## 7. Tasks, flows, checks, evidence

7.1 `task Name { input {...} output T require {...} ensure {...} budget {...}
allow {...} deny {...} }`.
- `input` is a record type; the flow parameter has this type.
- `output` is the type `complete` accepts; defaults to `Unit`.
- `require` predicates are evaluated before the flow body. Each is a pure or
  `fs.read`/`shell` expression yielding `Bool`. A false predicate fails with
  `Context`.
- `ensure` declares checks (7.3).
- `budget` and `allow` are the root scopes for the flow (§8). `deny` subtracts.

7.2 `flow Name(input) -> T { ... }` implements the task with the same name. A
package MAY contain several flows for one task in different modules; the CLI
selects one with `--flow module.Name`. A flow body MUST end by `complete`
(possibly inside branches) or by `fail`. Falling off the end is a compile error.

7.3 Checks. `check("name") by <method> [expect <pred>] [within <duration>]
[on_timeout <.Fail|.Record|.Skip>]`.
- Methods: `shell(cmd)` (result: `ShellResult { stdout, stderr, exit_code,
  duration }`), `test(cmd)` (result: `TestResult { passed, failed, skipped,
  output, exit_code }`), a call to a pure or effectful `fn` returning `Bool` or a
  record, `lint(...)`, `judge(agent, prompt)` (LLM-as-judge; produces
  non-deterministic evidence and is flagged as such).
- `expect` is a predicate over the method result's fields, bound by name
  (`stdout`, `exit_code`, `passed`, ...). Default: `exit_code == 0` for
  `shell`, `failed == 0` for `test`, the Bool itself for functions.
- `within` bounds wall time. Default 10m.
- `on_timeout`: `.Fail` (default) fails the check; `.Record` records a timeout
  as a non-passing but non-blocking evidence entry (see 7.5); `.Skip` marks
  skipped.

7.4 `verify ensure` runs all checks of the enclosing task; `verify ["a", "b"]`
runs named ones; `verify check(...) by ...` runs an inline check. Each execution
appends `Evidence { check, method, result: Pass|Fail|Timeout|Skipped, output,
duration, deterministic: Bool, requirement: String }` to the verification trace
and to the flow's evidence set. If any check result is `Fail`, `verify` fails
with `Verify(name)`. `Timeout` with `.Record` does not fail.

7.5 `complete value` checks, in order:
1. `value` has the task's output type;
2. every `ensure` check has at least one `Evidence` entry from **after the last
   `fs.write`/`run` that touched the repository** (stale evidence does not count);
3. every such entry is `Pass`, or `Timeout` under `.Record`, or `Skipped`
   under `.Skip`;
4. all spawned handles are joined.
If any condition fails, `complete` fails with `Verify`. Otherwise the flow
returns `value`, the outcome record is written and the run ends.

7.6 The outcome label (see `docs/11-episode-package-and-eval.md`) is computed
by the runtime from the evidence set, the intervention log and the failure, not
by the program.

## 8. Budgets and capabilities

8.1 `Budget { tokens: Int?, cost: Money?, wall: Duration?, tool_calls: Int?,
llm_calls: Int?, turns: Int? }`. Unset fields are unlimited.

8.2 Scopes. The task `budget` is the root. `budget {...} { block }` opens a
child scope (a non-literal budget or capability value must be parenthesised in
the block form, `budget (b) { block }`, to avoid ambiguity with named record
literals; the task-section form accepts `budget b` directly); `ask`/`run` options open a call scope. A child's limits MUST be
less than or equal to the parent's remaining amounts; otherwise `fail Budget` at
scope entry. Consumption is charged to every enclosing scope.

8.3 Enforcement. Before performing an effect the runtime checks the budget; an
effect that would exceed `tool_calls`/`llm_calls`/`turns` is not performed and
`fail Budget` is raised at that site. `tokens` and `cost` are checked after each
model turn (a single turn may overshoot; the overshoot is charged). `wall`
cancels in-flight effects at the deadline and fails with `Budget`.

8.4 `CapSet { fs.read: Globs, fs.write: Globs, shell: [Pattern], net: [Host],
human: Bool, spawn: Bool, env: [Name], secrets: [Name] }`. `Globs` is a glob
string or a list of globs, relative to the run's working directory; `[]` means
none; `"**"` means all. `Pattern` for `shell` is a glob over the full command
line. `Host` is a hostname with optional wildcard prefix.

8.5 Scopes narrow monotonically: `allow {...} { block }` MUST be a subset of the
enclosing set. `leash check` verifies subset relations statically when both sets
are literal, and reports where it cannot. The runtime verifies at scope entry
and fails with `Permission` on violation.

8.6 Every tool invocation (built-in, custom, MCP, driver-internal) is checked
against the active `CapSet`. Drivers receive the `CapSet` and MUST translate it
to their own sandbox settings (`--allowedTools`, `--sandbox`, `--add-dir` …) as
far as they can; the runtime remains the authority and denies anything the
driver lets through (for API/mock drivers by construction; for CLI drivers by
running them in a restricted working copy and post-checking the diff against
`fs.write`).

## 9. Failures and recovery

9.1 `Failure { kind: FailureKind, message: String, data: Json?, site: Site,
cause: Failure? }`. `FailureKind` = `Context | Tool | Feedback | Verify |
Recovery | Entropy | Model | Budget | Permission | Replay | Unknown`.

9.2 `fail Kind("msg")`, `fail Kind("msg", data)`, `fail expr` (re-raise a
`Failure`). `fail` has type `Never` and unifies with anything.

9.3 `attempt { } recover Kind as e { } ... recover _ as e { }`. Arms are tried in
order. An unmatched failure propagates. `recover` arms MAY re-raise.

9.4 `retry(n, on: Kind | [Kind] = [Tool, Feedback], backoff: .None |
.Fixed(d) | .Exponential(d)) { block }`. Runs the block up to `n` times while
it fails with a matching kind. `backoff` uses the `clock` effect (journaled).
The final failure propagates with `cause` set to the previous one.

9.5 Failures crossing `parallel`/`map`: the first failing branch (in branch
order, after all branches settle) propagates; other branches are cancelled or
completed per `parallel(on_fail: .CancelOthers | .WaitAll)`; default
`.CancelOthers`.

9.6 Runtime-raised failures use these kinds: budget exhaustion → `Budget`;
capability violation → `Permission`; tool error/timeout → `Tool`; schema or
refinement failure of a model result after retries → `Model`; `require` false →
`Context`; `verify`/`complete` → `Verify`; journal divergence → `Replay`; driver
transport errors after driver-level retries → `Tool` with `data.driver`.

## 10. Concurrency

10.1 `parallel { e1, e2, ... }` evaluates every branch concurrently and yields a
tuple in syntactic order. `parallel(limit: k)` bounds concurrency.

10.2 `race { e1, e2 }` yields the first branch to complete successfully; losers
are cancelled. The winner's index is journaled; replay returns the same branch.
All branches MUST have the same type.

10.3 `map(xs, concurrency: k) { x => e }` yields `[T]` in input order.

10.4 Branches are isolated: they may read enclosing immutable bindings; they
MUST NOT assign `var` locals or `state` fields of the enclosing scope (compile
error). Shared mutable state between branches is not supported in v1; use return
values.

10.5 Journal keys for branches are derived from the syntactic branch index and
the enclosing effect path, never from completion order (see determinism doc).

## 11. Type ↔ JSON Schema mapping

Used for structured outputs (`ask`/`run`), tool parameter schemas and
`--input` validation. The mapping MUST be deterministic and MUST produce
schemas accepted by the Anthropic structured-output and strict-tool validators
(`additionalProperties: false`, every property `required`).

| Leash | JSON Schema |
|---|---|
| `Int` | `{"type":"integer"}` |
| `Float` | `{"type":"number"}` |
| `Bool` | `{"type":"boolean"}` |
| `String` | `{"type":"string"}` |
| `Bytes` | `{"type":"string","contentEncoding":"base64"}` |
| `Duration` | `{"type":"string","pattern":"^[0-9]+(ms|s|m|h)$"}` |
| `Money` | `{"type":"object","properties":{"amount":{"type":"string"},"currency":{"type":"string"}}}` |
| `Unit` | `{"type":"null"}` |
| `Json` | `{}` |
| `T?` | `{"anyOf":[<T>,{"type":"null"}]}` |
| `[T]` | `{"type":"array","items":<T>}` |
| `(A,B)` | `{"type":"array","prefixItems":[<A>,<B>],"minItems":2,"maxItems":2}` |
| record | `{"type":"object","properties":{...},"required":[all],"additionalProperties":false}` |
| `Map<String,V>` | `{"type":"object","additionalProperties":<V>}` |
| enum, no payloads | `{"type":"string","enum":["A","B"]}` |
| enum, payloads | `{"oneOf":[{"type":"object","properties":{"kind":{"const":"A"},...},"required":[...],"additionalProperties":false}, ...]}` |
| refinements | not expressed; validated after parsing; the predicate text is appended to the field `description` |
| doc comments | `description` |

Field order in `properties` is declaration order. Schemas are cached by type
hash and included in the journal entry of the effect that used them.

## 12. Standard library surface (v1)

Modules and the most important members. Signatures are indicative; the stdlib
work package fixes them.

- `std.core`: `len`, `range`, `min`, `max`, `abs`, `to_string`, `parse_int`,
  `parse_json`, `to_json`, `hash`, list methods (`map filter any all find
  fold sorted join flat_map zip enumerate first last take drop`), string
  methods (`contains starts_with ends_with lines split trim replace lower upper
  matches`), option methods (`or_else unwrap_or is_null`), and the assertions
  used by `test` declarations: `assert(cond: Bool, message: String = "")` and
  `assert_eq<T>(a: T, b: T)`. A failed assertion raises
  `Failure { kind: Unknown, message, data: {"assert": true, "left": …, "right": …} }`;
  `leash test` renders it as an assertion failure rather than a crash.
- `std.drivers`: `anthropic(model, effort?, max_tokens?, thinking?, cache?,
  fallbacks?)`, `claude_code(model?, effort?, permission_mode?, max_turns?,
  mcp_config?, settings?)`, `codex(model?, sandbox?, profile?)`,
  `managed_agents(agent_id, environment_id?)`, `mock(script)`.
- `std.tools`: `fs.read fs.write fs.edit fs.glob fs.grep fs.mkdir fs.rm`,
  `shell`, `test`, `git.*`, `http.get/post`, `lint`.
- `std.repo`: `Repo` (`open(path)`, `is_clean`, `diff(since:)`, `commit`,
  `worktree`, `files`, `head`), `Patch` (`files`, `stats`, `apply`, `to_string`).
- `std.context`: `select(repo, files:, globs:, symbols:, memory:, max_tokens:,
  strategy:)`, `Context` (`files`, `tokens`, `hash`, `render()`).
- `std.verify`: `check`, `Evidence`, `evidence()`, `report()`.
- `std.failure`: `Failure`, `FailureKind`.
- `std.mcp`: `connect(stdio(cmd) | http(url))`, `McpServer.tool(name)`,
  `McpServer.tools()`.
- `std.human`: `gate(prompt) -> Bool`, `ask_human(prompt) -> String`.
- `std.trace`: `note`, `attribution`, `intervention`, `entropy`.
- `std.audit`: `entropy(repo, since:) -> EntropyReport`.
- `std.patterns`: `fix_verify_report`, `review`, `debate`, `triage`,
  `plan_execute`, `self_consistency`.
- `std.time`: `clock.now()`, `sleep(d)`.
- `std.env`: `env.get(name)`, `secrets.get(name)`.

## 13. Open questions

- Should `state` be allowed inside `parallel` branches with a merge function?
  (Deferred; v1 forbids.)
- Should `ask` support conversation continuation (`session`) for cheaper
  multi-turn interactions? Likely v1.1 via `Session` values.
- User-defined effects. Deferred.
- Should refinements be allowed on `type` aliases of primitives (`type Email =
  String where matches(...)`)? Probably yes; costs little.
- Money currencies beyond USD. Deferred.
