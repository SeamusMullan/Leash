# WP-15: Tasks, flows, checks, evidence, `complete`, report and outcome labels

Milestone: M4 · Size: L · Status: see `docs/status.md`

## Goal
The task contract becomes enforceable. `require` gates entry, `ensure`
declares checks, `verify` produces `Evidence`, `complete` refuses to finish
without fresh passing evidence, the runtime writes the verification trace and
`report.md`, and the trace layer computes the five-label outcome. With the
repoA fixture and the mock driver, `examples/02_fix_bug.leash` runs end to end
and yields `autonomous_verified_success`.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/06-verification-and-evidence.md` (all sections)
- `docs/02-language-spec.md` §7 (tasks, flows, checks, `complete`), §9.6
- `docs/11-episode-package-and-eval.md` §1–§2
- `docs/05-harness-model.md` §1 rows 1, 8, 10
- `docs/04-determinism-and-replay.md` §7 (snapshots referenced by evidence)
- `docs/research/paper-notes.md` §1 (repoA-T1 task description)
- WP-10 (LIR nodes for task sections), WP-11 (shell/test tools, snapshots), WP-12 (scopes)

## Scope
- Interpreter support for `TaskDecl` sections: bind `input` type, `output`
  type, evaluate `require` predicates before the flow body (false or failing
  predicate → `Failure { kind: Context, message: "<predicate source>" }`),
  install root `BudgetScope`/`CapScope` from `budget`/`allow`/`deny`.
- Check model: `CheckMethod::{Shell, Test, Fn, Lint, Judge}` with result
  records bound by field name for `expect`; defaults per docs/06 §2 table;
  `within` (default 10m) and `on_timeout` (`.Fail` default, `.Record`, `.Skip`).
- `verify ensure`, `verify ["a", "b"]`, `verify check(...) by ...` (inline).
  Each executed check appends an `Evidence` record (docs/06 §3 fields incl.
  `deterministic`, `confidence`, `scope`, `repo_state`, `at_seq`, `effect_id`)
  to the verification trace and the flow's `EvidenceSet`. Any `fail` result →
  `Failure { kind: Verify, message: "<check name>" }` after all checks ran.
- Freshness: `RunState.last_mutating_seq` updated by `fs.write` effects, `run`
  entries whose caps include `fs.write`, and mutating `git` tools; evidence with
  `at_seq <= last_mutating_seq` is stale.
- `complete value`: the five conditions of docs/06 §5 in order; on failure a
  `Verify` failure whose `data` lists unmet, stale and non-deterministic
  requirements. On success: write `traces/outcome.json`, `artifacts/patch.diff`
  (working copy vs. run start), `report.md`, then end the run.
- `judge(agent, prompt)` method implemented over `ask` with a fixed `Verdict
  { verdict: Bool, rationale: String }` schema; evidence `deterministic: false`,
  `confidence: "low"`; a task-level `judge_policy` (default `.Advisory`) caps
  the label at `unverified_success`; `.AcceptAsEvidence` lets judge evidence
  satisfy `complete` and the report states the lower assurance.
- Pre-complete hook registry: `PreCompleteHook` trait; `complete` runs hooks
  before condition checks; a hook may append evidence or fail. WP-21 registers
  the entropy audit; this WP ships a no-op default and the config key
  `[verify] entropy_audit = "warn" | "fail" | "off"`.
- Outcome label computation per docs/06 §6 in `leash-trace`, with a
  `justification` array naming the evidence ids, interventions and audit
  items that decided it.
- `report.md` generation from `std/templates/report.md` (a minimal template
  engine; `minijinja` is acceptable) with the table, limitations, attribution,
  interventions, entropy and budget sections shown in docs/06 §7.
- Fixture `tests/fixtures/repoA`: a small Node.js login app using the built-in
  `node --test` runner (no npm install): `src/controller.js`, `src/validator.js`
  (accepts empty passwords: the defect), `src/auth.js`, `src/ui.js`,
  `test/login.test.js`, `harness/probe.js` (`node harness/probe.js empty|valid|invalid`
  prints the controller's JSON response), `package.json` with `"test": "node --test test/"`.
  Plus `tests/fixtures/repoA/evaluator/checks.toml` (hidden checks) and a mock
  driver script `tests/fixtures/repoA/mock/coder.json` whose scripted `run`
  edits `src/validator.js` and adds a test through the tool layer.

## Out of scope
- The entropy audit heuristics (WP-21).
- Context selection and profiles (WP-17); the fixture run uses `select` only
  if WP-17 has landed, otherwise a plain task string.
- `leash eval` and adjudication with evaluator checks (WP-26); the
  `checks.toml` file is only created here.
- Test-runner parsers beyond what WP-11 provides; add the `node --test`
  (TAP) parser to WP-11's module if missing and note it in the PR.

## Interfaces
```rust
pub enum CheckMethod { Shell { cmd: String }, Test { cmd: String, runner: Option<Runner> }, Lint { cmd: Option<String> },
                       Fn { closure: LirClosure, args: Vec<Value> }, Judge { agent: AgentRef, prompt: String } }
pub struct Check { pub name: String, pub method: CheckMethod, pub expect: Option<LirClosure>, pub within: Duration, pub on_timeout: OnTimeout }
pub enum OnTimeout { Fail, Record, Skip }
pub enum EvidenceResult { Pass, Fail, Timeout, Skipped, Error }
pub struct Evidence { pub check: String, pub requirement: String, pub method: serde_json::Value, pub result: EvidenceResult,
                      pub deterministic: bool, pub confidence: Confidence, pub scope: Option<String>, pub output_ref: Option<CasRef>,
                      pub excerpt: String, pub duration_ms: u64, pub effect_id: EffectId, pub repo_state: Option<String>, pub at_seq: u64 }
pub struct EvidenceSet { /* by check name, newest last */ }
pub trait PreCompleteHook: Send + Sync { async fn run(&self, ctx: &CompleteCtx) -> Result<Vec<Evidence>, Failure>; }
pub enum OutcomeLabel { AutonomousVerifiedSuccess, AssistedVerifiedSuccess, UnverifiedSuccess, Failed, UnsafeInvalid }
pub struct Outcome { pub label: OutcomeLabel, pub justification: Vec<String>, pub budget: BudgetTotals, pub metrics: serde_json::Value }
```
Files: `traces/verification.jsonl` (one `Evidence` per line), `traces/outcome.json`, `artifacts/report.md`, `artifacts/patch.diff`.

## Deliverables
- `crates/leash-runtime/src/{task.rs, verify.rs, complete.rs, freshness.rs}`
- `crates/leash-trace/src/{outcome.rs, report.rs}`, `std/templates/report.md`
- `tests/fixtures/repoA/**` and `tests/integration/fix_bug_repoa.rs`
- Docs: `docs/06-verification-and-evidence.md` updated with the `judge_policy`
  syntax chosen (see pitfalls) and any field added to `Evidence`.

## Acceptance criteria
- `cargo nextest run -p leash-runtime -p leash-trace` passes with unit tests
  for: default predicates per method; `on_timeout` variants; freshness (an
  `fs.write` after `verify` makes `complete` fail with a message naming the
  stale check); the five `complete` conditions each failing in isolation.
- `leash run examples/02_fix_bug.leash --input @tests/fixtures/repoA/input.json --driver anthropic=mock:tests/fixtures/repoA/mock/coder.json --gate deny`
  exits 0; `traces/outcome.json.label == "autonomous_verified_success"`;
  `report.md` lists five requirements with three `pass`, one `pass` (targeted
  tests) and the full regression as `pass` or `timeout (recorded)`; `patch.diff`
  touches only `src/validator.js` and `test/`.
- Replacing the mock script with one that edits `src/controller.js` instead
  (wrong layer) yields `failed` with `Verify("empty password rejected")` in
  the attribution trace.
- A judge-only check with default policy yields `unverified_success` and the
  report says why.
- `leash replay <run>` reproduces the same `outcome.json` byte for byte
  except timestamps.

## Notes and pitfalls
- `judge_policy` is mentioned in docs/06 §9 but has no grammar slot. Add a
  task section `options { judge_policy: .Advisory }` to the parser (coordinate
  with WP-07; a one-line grammar addition) or, if WP-07 is closed, accept it
  under `[verify] judge_policy` in `leash.toml` and open a follow-up.
- `expect` predicates are pure closures over the method result; the checker
  (WP-08) types them against the method's result record. Evaluate them with
  the interpreter, never by string matching.
- Evidence must reference the working-copy snapshot it ran against; take a
  snapshot before `verify` if the last one is older than `last_mutating_seq`.
- Flows that end without `complete` or `fail` are compile errors (WP-09);
  the runtime still guards with an `Unknown` failure.
- The `node --test` runner prints TAP; `passed`/`failed` come from the
  summary lines `# pass N` / `# fail N`.
- `on_timeout .Record` must never count as `pass`; the label logic treats it
  as "passing evidence with a recorded limitation" only when
  `deterministic == true` and the requirement has no other failing evidence.

## Dependencies
Hard: WP-10, WP-11, WP-12, WP-05 (mock driver with scripted tool calls; extend it here if WP-05 shipped `ask` scripts only). Soft: WP-16 (`retry`/`attempt` used by the example), WP-17.

## Estimated size
L (1–2 weeks).
