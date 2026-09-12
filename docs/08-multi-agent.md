# Multi-Agent Programs in Leash

Status: draft v0.1. Sources: "Code as Agent Harness" §4 (roles, interaction
modes, topologies, synchronisation, convergence), SIA (meta-agent /
feedback-agent split).

## 1. Position

Leash does not ship an agent "framework" with managers and message buses. A
multi-agent system is a Leash program whose control flow spawns and joins
agents. Roles are agent values, topologies are code, synchronisation is return
values, and convergence is a predicate the program evaluates. The runtime
contributes deterministic scheduling, journaling and structured cancellation.

The survey's observation that "topology complexity is partially a symptom: when
the substrate is formally represented and queryable, agents can coordinate
through simple, transparent protocols" is the design bet: give agents a shared
repository, typed results and an explicit verifier, and keep the coordination
logic in plain code.

## 2. Roles

`std.patterns.roles` provides presets that only set system prompts and tool
sets: `planner`, `coder`, `reviewer`, `tester`, `attributor`, `judge`,
`manager`. They are ordinary `Agent` values users copy and modify. Nothing in
the runtime knows about roles.

## 3. Interaction modes as stdlib patterns

All patterns are Leash source in `std/patterns/`, small enough to read in a
minute, and every one has an explicit convergence criterion (the survey calls
implicit convergence "the most significant gap in the field").

| Pattern | Signature (indicative) | Convergence |
|---|---|---|
| `fix_verify_report(coder, attributor, task, max_attempts)` | reproduce → attribute → fix → verify → report with back-edge | verify passes or attempts exhausted → `Recovery` |
| `review(patch, reviewers: [Agent], policy: .AllApprove \| .Majority)` | parallel independent reviews | policy over `Review.blocking` |
| `critique_repair(coder, critic, artifact, until: pred, max_rounds)` | alternate generate/critique | `until(artifact)` or rounds → `Recovery` |
| `debate(a, b, topic, rounds) -> Transcript` + `judge` | adversarial | fixed rounds, then a typed verdict |
| `self_consistency(agent, prompt, n, pick: fn([T]) -> T)` | n independent samples | deterministic picker (majority, min cost, …) |
| `plan_execute(planner, workers, task)` | planner emits typed `Plan { steps: [Step] }`; workers run steps by dependency order (`map` over topological levels) | all steps verified |
| `triage(agent, items, concurrency)` | fan-out | all items processed or budget |
| `tournament(candidates, judge)` | pairwise/bracket | single winner |

Independent test design (AgentCoder) is a pattern too: `independent_tests(tester, spec)`
produces tests *before* the coder sees them, and `ensure` binds them as checks,
so the "agent's biased tests pass its own buggy code" failure is structurally
avoided.

## 4. Topologies

- Chain/waterfall: sequential code.
- Cyclic: `loop`/`retry` with a convergence predicate.
- Hierarchical: a manager flow spawning sub-flows (`spawn run` or calling flows
  as functions).
- Star/blackboard: not in v1 as shared mutable state; emulate with a manager
  that owns the state and passes typed snapshots.
- Adaptive DAG: a planner emits a `Plan` value; the program executes it with
  `map` per dependency level; re-planning is a loop. The DAG is data in the
  journal, so it is replayable and reviewable.

## 5. Synchronisation and shared state

v1 rules (spec §10.4): branches are isolated; they communicate by return
values. The working copy is the only shared substrate and only one `run` with
write capability may target the same working copy at a time (the runtime
serialises them and journals the order). To parallelise edits, give each branch
its own worktree (`repo.worktree()`) and merge deterministically in the parent
(`git` tool), followed by re-verification.

v2 (roadmap M11) implements the survey's transactional proposal: each `run`
declares a read set and write set (already implicit in `allow`), the runtime
detects overlapping write sets across concurrent runs, merges with a declared
policy (`.Serialize | .Merge | .Fail`), re-runs `verify` after merge, and
records conflicts and rollbacks. A shared `blackboard` value with
compare-and-set semantics is under consideration for the same milestone.

## 6. Budgets and failure across agents

A `parallel`/`map` shares the enclosing budget unless partitioned. Failure
propagation is by branch order after settlement (spec §9.5). `race` losers are
cancelled and their partial cost is charged. A sub-flow invoked as a function
runs in the caller's journal with a nested path; a `spawn`ed sub-flow does too.
There is one journal per run, always.

## 7. Meta-agents

The SIA split (Meta-Agent creates the scaffold, Feedback-Agent rewrites it from
trajectories and metrics) is expressible because a flow can read episode
packages (`std.runs.load(id)`) and emit source or LIR edits. See
`docs/05-harness-model.md` §6 and roadmap M10. v1 only guarantees the inputs
exist: replayable journals, stable LIR, fixed verifiers.

## 8. Evaluation of multi-agent programs

Beyond outcome labels: trajectory efficiency (tool calls, tokens, edits, wall),
verification strength (coverage, oracle diversity), recovery ability, state
consistency (merge success, semantic regression rate, rollback frequency),
safety compliance, replayability. These are computed from the journal by
`leash eval` (`docs/11-episode-package-and-eval.md`).
