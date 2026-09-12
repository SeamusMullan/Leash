# Episode Package and Evaluation

Status: draft v0.1. Source: AI Harness Engineering (trace-based evaluation),
Code as Agent Harness §5.2.1 (harness-level metrics).

## 1. Episode package

An episode is one attempt of one flow on one task input under one profile with
one driver configuration. Its package is `.leash/runs/<run_id>/`:

| File | Content | Derived from |
|---|---|---|
| `manifest.json` | identity, versions, program hash, inputs hash, profile, drivers, repo, seed | run start |
| `journal.jsonl` | every effect | the run (source of truth) |
| `traces/action.jsonl` | externally meaningful operations: `read_file edit_file run_tool ask run verify checkpoint gate complete fail` | journal |
| `traces/tool.jsonl` | command/tool, args, exit code, duration, timeout, failure kind, recovered? | journal children |
| `traces/context.jsonl` | which files/memory artifacts were selected, tokens, hash, whether a later effect referenced them | `select` entries |
| `traces/verification.jsonl` | Evidence entries | `verify` |
| `traces/attribution.jsonl` | Attribution records and every `Failure` with kind | `trace.attribution`, failures |
| `traces/intervention.jsonl` | gates and human answers, avoidability, harness gap | `human` effects |
| `traces/entropy.jsonl` | audit items with category and severity | `audit.entropy` |
| `traces/outcome.json` | label, justification, metrics summary, budget totals | all of the above |
| `artifacts/` | `patch.diff`, `task_state.json`, `report.md`, files the program saved with `artifacts.save` | program + runtime |

Trace schemas live in `spec/traces/*.schema.json`. Traces are regenerable from
the journal (`leash report --regenerate`), so they are never the source of
truth and can evolve without invalidating old runs.

## 2. Outcome label

See `docs/06-verification-and-evidence.md` §6. The label is written with a
justification listing the evidence entries, interventions and audit items that
determined it.

## 3. Metrics (per episode, then aggregated per cell)

A cell is `(task, flow, profile, driver/model)`.

| Metric | Definition |
|---|---|
| AVSR | fraction of episodes labelled `autonomous_verified_success` |
| M-HIR | fraction of episodes with ≥1 intervention marked `avoidable` |
| verification autonomy | requirements with deterministic passing evidence / requirements declared |
| context-trace meaningfulness | selected artifacts later referenced by an edit or a verification / selected artifacts |
| tool recovery rate | tool failures followed by a successful retry of the same intent / tool failures |
| failure attribution completeness | failures with a full Attribution record / failures |
| entropy delta | sum of severity of audit items at completion |
| trajectory efficiency | tool calls, model turns, tokens, cost, wall, edits per episode |
| verification strength | oracle diversity (distinct method kinds), test coverage delta when available, false-acceptance proxy (judge-only evidence count) |
| recovery ability | retries needed, `Recovery` failures |
| state consistency (multi-agent) | merge conflicts, reverts, re-verifications |
| safety compliance | `Permission` events, reverted out-of-scope edits |
| replayability | replay succeeds without divergence (always expected true) |

`leash eval` emits `metrics.csv` (one row per cell), `episodes.csv` (one row per
episode) and `metrics.json`. Column names are fixed:

```
suite,task,flow,profile,driver,model,episodes,avsr,avsr_ci_low,avsr_ci_high,m_hir,
verification_autonomy,context_meaningfulness,tool_recovery_rate,attribution_completeness,
entropy_delta,tool_calls,model_turns,tokens,cost_usd,wall_ms,edits,oracle_diversity,
judge_only_evidence,retries,recovery_failures,permission_events,reverted_edits,
replay_ok_rate,label_agreement_rate
```

`episodes.csv` carries the same metric columns plus `run_id,episode_index,
autonomous_label,adjudicated_label,failure_kind`. Bootstrap 95% confidence
intervals are computed only when a cell has at least ten episodes; below that
the `*_ci_low`/`*_ci_high` columns are empty rather than misleadingly narrow.
The shipped ladder suite uses fewer episodes per cell, so its CI columns are
empty by design; raise `episodes_per_cell` to ten or more when you want them.

## 4. Suites

```toml
# suites/validation.toml
[suite]
name = "validation-tasks"
profiles = ["h0", "h1", "h2", "h3"]
episodes_per_cell = 5            # ≥ 10 to populate the confidence-interval columns
drivers = ["mock:fixtures/mock"] # cell dimension; "anthropic:claude-opus-5" for live runs
hidden_paths = ["evaluator/**"]  # deleted from every episode worktree before the run

[[task]]
id = "repoA-T1"
flow = "flows/fix_bug.leash:FixEmptyPassword"
repo = { git = "fixtures/repoA", rev = "a1b2c3" }
input = { issue = "Empty password is not rejected …" }
evaluator_checks = "fixtures/repoA/evaluator/checks.toml"   # hidden from the agent at every profile

[[task]]
id = "repoB-T3"
…
```

Evaluator checks are hidden by construction, not by convention. Before an
episode starts the runner creates the worktree, deletes every path matching
`hidden_paths` and asserts they are absent, so no capability set can reach them.
After the episode the runner restores the evaluator files into a separate
adjudication sandbox seeded from the episode's final snapshot
(`docs/04-determinism-and-replay.md` §7) and runs `checks.toml` there.
Adjudication effects are journaled to `adjudication.jsonl`, never to
`journal.jsonl`, so the episode package stays a faithful record of what the
agent did. The evaluator checks produce the adjudicated label; the program's own
evidence produces the autonomous label. Both are reported, and their
disagreement is the `label_agreement_rate` column, because "patch correct but
unverified" is the case this project cares about.

## 5. Benchmarks

- **Ladder suite** (ships with the repo): small constructed repositories with
  controlled defects, one per harness component (validation → verification;
  UI behaviour → observability; dependency cleanup → entropy; refactor →
  memory; flaky test → attribution/recovery; long feature → task state;
  permission-sensitive → capabilities).
- **SWE-bench Lite adapter** (`leash-eval/adapters/swebench.rs`): converts
  instances to tasks with `ensure` = FAIL_TO_PASS/PASS_TO_PASS tests as
  `test` checks. Post-v1 milestone.
- Long-horizon suite: sequences of tasks on the same repository with entropy
  audits between episodes. Post-v1.

## 6. Comparing runs

`leash diff run-a run-b` aligns journals by effect id, shows changed inputs,
outputs, costs and outcomes, and highlights the first divergence. This is the
primary tool for "did my harness change help?" questions and for reviewing
evolved flows.
