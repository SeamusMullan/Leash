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
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
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
- Check model: `CheckMethod` with `Shell`, `Test`, `Fn`, `Lint` and `Judge`
  alternatives; result records bound by field name for `expect`; defaults per
  docs/06 §2 table; `within` (default 10 min) and `on_timeout` (`.Fail`
  default, `.Record`, `.Skip`).
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
- Pre-complete hook registry: `PreCompleteHook` abstract class; `complete` runs
  hooks before condition checks; a hook may append evidence or fail. WP-21
  registers the entropy audit; this WP ships a no-op default and the config key
  `[verify] entropy_audit = "warn" | "fail" | "off"`.
- Outcome label computation per docs/06 §6 in `leash_trace`, with a
  `justification` array naming the evidence ids, interventions and audit
  items that decided it.
- `report.md` generation from `std/templates/report.md` with a hand-written
  minimal renderer (section substitution and fixed tables only; no template
  dependency) with the table, limitations, attribution, interventions, entropy
  and budget sections shown in docs/06 §7.
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
```cpp
// src/spec/include/leash/spec/check.hpp
struct CheckMethod {
    struct Shell { std::string cmd; };
    struct Test  { std::string cmd; std::optional<Runner> runner; };
    struct Lint  { std::optional<std::string> cmd; };
    struct Fn    { LirClosure closure; std::vector<Value> args; };
    struct Judge { AgentRef agent; std::string prompt; };

    std::variant<Shell, Test, Lint, Fn, Judge> value;
};

enum class OnTimeout { fail, record, skip };
enum class EvidenceResult { pass, fail, timeout, skipped, error };

struct Check {
    std::string name;
    CheckMethod method;
    std::optional<LirClosure> expect;
    std::chrono::nanoseconds within = std::chrono::minutes(10);
    OnTimeout on_timeout = OnTimeout::fail;
};

// src/trace/include/leash/trace/evidence.hpp
struct Evidence {
    std::string check;
    std::string requirement;
    nlohmann::json method;                  // the serialised CheckMethod that ran
    EvidenceResult result;
    bool deterministic = false;
    Confidence confidence;
    std::optional<std::string> scope;
    std::optional<PayloadRef> output_ref;
    std::string excerpt;
    std::uint64_t duration_ms = 0;
    EffectId effect_id;
    std::optional<std::string> repo_state;
    std::uint64_t at_seq = 0;
};

class EvidenceSet {                         // by check name, newest last
public:
    void append(Evidence evidence);
    std::span<const Evidence> by_check(std::string_view check) const;
};

class PreCompleteHook {                     // called on the run's strand
public:
    virtual ~PreCompleteHook() = default;
    virtual asio::awaitable<std::expected<std::vector<Evidence>, Failure>>
    run(const CompleteCtx& ctx) = 0;
};

enum class OutcomeLabel {
    autonomous_verified_success, assisted_verified_success, unverified_success,
    failed, unsafe_invalid
};

struct Outcome {
    OutcomeLabel label;
    std::vector<std::string> justification;
    BudgetTotals budget;
    nlohmann::json metrics;
};
```
`Check`, `CheckMethod`, `OnTimeout` and `EvidenceResult` belong to `leash_spec`;
`Evidence`, `EvidenceSet`, `PreCompleteHook` and `Outcome` belong to
`leash_trace`. `Runner`, `Confidence`, `AgentRef`, `LirClosure`, `EffectId` and
`BudgetTotals` are already defined; do not redefine them here.

Files: `traces/verification.jsonl` (one `Evidence` JSON object per line), `traces/outcome.json`, `artifacts/report.md`, `artifacts/patch.diff`.

## Deliverables
- `src/runtime/{task.cpp, verify.cpp, complete.cpp, freshness.cpp}` with headers under `src/runtime/include/leash/runtime/`
- `src/trace/{outcome.cpp, report.cpp}`, `std/templates/report.md`
- `tests/fixtures/repoA/**` and `tests/integration/fix_bug_repoa_test.cpp`
- Docs: `docs/06-verification-and-evidence.md` updated with the `judge_policy`
  syntax chosen (see pitfalls) and any field added to `Evidence`.

## Acceptance criteria
- `ctest --preset dev -L 'runtime|trace'` passes with unit tests
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
