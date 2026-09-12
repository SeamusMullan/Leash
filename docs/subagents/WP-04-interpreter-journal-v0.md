# WP-04: Interpreter core, journal, replay/resume (v0 subset)

Milestone: M1 · Size: L · Status: see `docs/status.md`

## Goal
Execute v0 programs end to end with the determinism model in place from day
one: a tree-walking evaluator, an effect gateway, a journal with structural
effect ids, and `leash run`, `leash replay`, `leash resume`. After this WP,
`examples/01_hello.leash` runs against a driver, and replaying its journal
reproduces the outcome with no network.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/04-determinism-and-replay.md` (all; §2 and §4 are the algorithms to implement)
- `docs/03-runtime-architecture.md` §5–§7
- `docs/02-language-spec.md` §4 (expressions), §5 (effects), §6.2 (`ask`), §9 (failures), §11 (schema mapping, minimal subset here)
- `spec/journal.schema.json` (WP-02), `docs/10-cli-and-tooling.md` §1 (exit codes, flags)
- `docs/adr/0002`, `0003`, `0008`

## Scope
- `crates/leash-runtime`:
  - `program.rs`: the evaluator's input model for v0 (`Program`, `Flow`,
    `Fn`, `Agent`, `Task`, `Expr`, `Stmt`, `TypeRef`) and `from_ast(&leash_syntax::ast::Program) -> Result<Program, Failure>`.
    The evaluator depends only on `program.rs`; WP-10 replaces `from_ast` with
    `from_lir`. Write this migration note at the top of the module.
  - `value.rs`: `Value` per docs/03 §6.1 (v0 needs Unit, Bool, Int, Float,
    Str, Duration, Money, List, Record, Enum(name only), Fn, Opaque, Null),
    canonical JSON conversion (`to_canonical_json`: sorted keys, no floats
    with NaN, `Money` as decimal string), structural equality.
  - `env.rs`: lexical scopes, immutable bindings, closures capturing by value.
  - `eval.rs`: `async fn eval(&mut self, e: &Expr) -> Result<Value, Control>`
    with `Control::{Fail(Failure), Complete(Value)}`; built-ins for v0:
    `len`, `usd`, `to_string`, list `filter/map/any/all/first`, string
    `contains/lines/trim`; `if`; calls with named args and defaults; lambdas;
    UFCS dispatch to built-in methods.
  - `effect.rs`: `EffectKind`, `EffectRequest { path: EffectPath, kind, input: serde_json::Value, site: Site }`,
    `EffectResult`, `EffectGateway::perform(req) -> Result<EffectResult, Failure>` implementing the docs/04 §4 algorithm
    for modes `Live | Replay | Resume` (`Fork` is WP-16).
  - `path.rs`: activation stack and occurrence counters producing the
    `EffectPath` of docs/04 §2; `effect_id = blake3(canonical_json(path))`
    rendered as `b3:<hex>`; `site_id` for v0 = `"{module}::{item}#{node_kind}@L{line}"`
    (WP-10 supplies structural ids; keep the function in one place).
  - `journal.rs`: `Journal::create(path)`, `open(path)`, `append_started(entry)`,
    `append_finished(entry)` (fsync after each line), `index() -> HashMap<EffectId, JournalEntry>`
    (last entry wins), line validation against `leash_spec::journal` in
    debug builds; `PayloadStore` trait with `InlineStore` (everything inline;
    WP-06 adds CAS) and the 8 KiB threshold constant.
  - `schema.rs`: v0 type annotation → JSON Schema for records of primitives,
    lists, optionals, named record types (spec §11 rows for those); hash of
    the canonical schema. WP-08 replaces this module.
  - `failure.rs`: `Failure { kind: FailureKind, message, data, site, cause }` and `FailureKind` (all eleven kinds).
  - `run.rs`: `Runner::new(program, drivers: DriverRegistry, mode, journal_path).run(flow, input: Value) -> RunOutcome`
    where `RunOutcome { status: Completed(Value) | Failed(Failure), effects: u64 }`;
    `require`/`ensure`/budgets are ignored in v0 but the task `budget` record is written into the journal's first entry as `note`.
  - `ask` semantics of docs/02 §6.2 for `String` and record/list results:
    schema derived from the `let` annotation, driver call, parse, validate
    against the schema (`jsonschema`), on failure re-ask with the error
    appended up to `retries` (default 2), each attempt a separate effect with
    its own occurrence.
- `crates/leash-cli`: `run <file> [--flow NAME] [--input JSON|@file] [--driver KIND=mock:PATH]... [--offline]`,
  `replay <run-id-or-path>`, `resume <run-id-or-path>`. `--offline` installs a
  gateway guard that fails any live effect with `Replay`. Run directory for now:
  `.leash/runs/<UTC>_<flow>_<6hex>/journal.jsonl` (WP-06 formalises the rest).
- Exit codes per docs/10 §1 (0, 1, 2, 3).

## Out of scope
Budgets and capabilities (WP-12), `run`/tools (WP-11/13), CAS and traces
(WP-06), fork/checkpoint/state (WP-16), concurrency (WP-20), the full type
checker (WP-08), LIR (WP-10), any driver code beyond calling the WP-05 trait.

## Interfaces
```rust
pub enum Mode { Live, Replay, Resume }
pub struct EffectPath(pub Vec<(SiteId, u32)>);
pub struct EffectId(pub [u8; 32]);   // Display as "b3:<hex>"
pub trait PayloadStore: Send + Sync { fn put(&self, bytes: &[u8]) -> PayloadRef; fn get(&self, r: &PayloadRef) -> Option<Vec<u8>>; }
pub enum PayloadRef { Inline(serde_json::Value), Cas(String) }
pub struct EffectCtx<'a> { pub run_id: &'a str, pub cancel: tokio_util::sync::CancellationToken, pub store: &'a dyn PayloadStore }
pub struct DriverRegistry { /* name -> Arc<dyn leash_drivers::Driver> */ }
impl Runner { pub async fn run(&mut self, flow: &str, input: Value) -> RunOutcome; }
```
The runtime depends on `leash-drivers` (WP-05) for the `Driver` trait and
adapts `EffectCtx` to `leash_drivers::DriverCtx`. Until WP-05 lands, code
against the trait signature in WP-05's brief and use a local stub in tests.

## Deliverables
- `crates/leash-runtime` with the modules above and unit tests.
- CLI subcommands `run`, `replay`, `resume`.
- `tests/fixtures/mock/hello.json` mock script for `examples/01_hello.leash` and an integration test crate `tests/` (workspace member `leash-integration`) with the tests below.

## Acceptance criteria
- `cargo run -p leash-cli -- run examples/01_hello.leash --input '{"topic":"agent harness"}' --driver anthropic=mock:tests/fixtures/mock/hello.json`
  prints a run id and `outcome: completed` and writes `journal.jsonl` whose every line validates against `spec/journal.schema.json`.
- `env -u ANTHROPIC_API_KEY cargo run -p leash-cli -- replay <run-id> --offline` exits 0, performs zero driver calls (assert via a counting mock in the integration test), and prints the same completed value.
- Integration test `live_then_replay_round_trip`: run with mock → replay → identical `RunOutcome`, identical effect id multiset.
- Integration test `shuffled_journal_replays`: shuffle journal lines (keeping started/finished order per id) → replay succeeds with identical outcome.
- Integration test `crash_then_resume`: mock script fails the second `ask` with an injected panic hook (`Runner::set_crash_at(seq)`); `resume` completes the run; the journal shows the interrupted entry as `started` followed by a fresh `ok` line.
- Integration test `divergence_detected`: change the prompt text in the program, replay the old journal → exit code 3 and a message naming the site and showing the input hash mismatch.
- Integration test `ask_validation_retry`: mock returns invalid JSON once, then valid → two `llm.ask` effects with occurrences 0 and 1; final value typed.
- `cargo nextest run -p leash-runtime -p leash-integration` passes; clippy clean.

## Notes and pitfalls
- fsync the journal before returning any effect result to the evaluator (docs/03 §7).
- Never key journal lookups by `seq`; only by `EffectId`. Keep `seq` for humans.
- `serde_json::Value` map ordering: build canonical JSON with a `BTreeMap`
  conversion; do not rely on `preserve_order`.
- Floats in canonical JSON: use shortest round-trip formatting; reject NaN/Inf
  at construction with an `Unknown` failure.
- Do not implement `Fork` yet, but make `Mode` an enum so WP-16 adds it.
- The `started` line must be written before the driver is called; the
  `finished` line replaces it logically (same id, later `seq`).
- Keep `eval.rs` free of tokio primitives other than `.await`; concurrency
  arrives in WP-20 through the gateway and a scheduler module.
- Secrets redaction is WP-12; leave a `redact()` hook in `journal.rs` that is
  the identity for now, and call it on every payload.

## Dependencies
Hard: WP-02, WP-03. Soft: WP-05 (needed for the live acceptance criterion; the round-trip tests can use an in-crate stub driver).

## Estimated size
L (1–2 weeks).
