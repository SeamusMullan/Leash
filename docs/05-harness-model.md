# The Harness Model: How Leash Covers the Eleven Responsibilities

Status: draft v0.1. This document maps the research vocabulary in
`docs/research/paper-notes.md` onto Leash constructs and runtime services, and
defines the H0–H3 harness profiles.

## 1. The eleven responsibilities (AI Harness Engineering, Table 1)

| # | Responsibility | Runtime contract | Leash construct / service | Evidence produced |
|---|---|---|---|---|
| 1 | Task interface | present objective, requirements, constraints, success criteria | `task { input output require ensure budget allow }` | task record in manifest; `ensure` list |
| 2 | Context manager | select and expose task-relevant content | `select(...) -> Context`; `Context` is hashed, sorted, budgeted | context trace |
| 3 | Tool registry | declare available tools and allowed commands | `tools: [...]` on agents/runs; `allow.shell` patterns; MCP schema cache | tool trace |
| 4 | Project memory | agent-readable architecture, testing, known failures | `memory x = file(...)`; `select(memory: [...])`; materialised into the agent workdir per profile | context trace (memory entries) |
| 5 | Task state | hypothesis, inspected files, open questions, next steps | `state { ... }` in flows; snapshotted at checkpoints; materialised as `TASK_STATE.md` per profile | `state` snapshots in journal; `artifacts/task_state.json` |
| 6 | Observability | expose logs, traces, outputs, runtime errors | journal + eight traces; `ShellResult`/`TestResult` returned to the program; driver events | all traces |
| 7 | Failure attribution | separate observation, expected, diagnosis | `Attribution` record in std; `trace.attribution`; failure kinds on every `Failure` | attribution trace |
| 8 | Verification protocol | map requirements to deterministic evidence | `ensure` checks, `verify`, `Evidence`, `complete` rule | verification trace, report |
| 9 | Permission boundary | restrict risky actions, approval gates | `allow`/`deny` CapSets, sandbox, `gate` | permission records in journal; intervention log |
| 10 | Entropy auditor | detect maintenance burden | `audit.entropy`; automatic pre-`complete` audit | entropy trace |
| 11 | Intervention logger | record human assistance and avoidability | `gate`/`ask_human` journaling; `trace.intervention(avoidable:, gap:)` | intervention log; feeds outcome label |

The runtime resources of Table 2 map similarly: context budget → `select(max_tokens:)`;
tool budget → `Budget.tool_calls` + `allow`; verification evidence → `Evidence`;
human attention → intervention log + `Budget` (post-v1: `human_minutes`);
failure signal → typed results and `Failure`; entropy budget → audit thresholds;
test-time compute → `Budget.tokens/cost/wall`.

## 2. Plan–Execute–Verify as Leash structure

"Code as Agent Harness" frames harness control as a PEV loop with planning as
*contract formation*: relevant files, invariants, validation commands, rollback
points, risky operations, allowed read/edit sets, completion criteria. In Leash
the contract is the `task` block, formed by the programmer (or a planner agent
producing a typed `Plan` value that the program turns into `select`/`allow`
arguments); execution is `run`/tool effects under the sandbox; verification is
`verify`/`complete`. The loop's back-edges are ordinary `retry`/`attempt`.

Permission tiers from the survey (read-only, sandbox-edit, full-access with
mandatory human gates) are CapSet presets in `std.caps`:

```leash
// task-section form
allow caps.read_only                                                   // fs.read: "**"
allow caps.sandbox_edit(["src/**", "test/**"], shell: ["npm test*"])
allow caps.full_access                                                 // requires human: true; net, git push and rm are gated

// block form: a non-literal must be parenthesised (grammar: AllowExpr)
allow (caps.read_only) {
  let notes = run reviewer(task: "Read-only review of {patch}")
}
```

Tool lifecycle hooks (pre-use: permission check, argument validation, gate;
post-use: sanitise, compact, offload, update memory, trigger verification) are
runtime middleware on the tool layer. Programs attach hooks with
`tool.before(fn)` / `tool.after(fn)` (std); the built-in permission and
journaling hooks always run first and last.

## 3. Profiles H0–H3

A profile controls which harness supports are *exposed to the model* (not
which the runtime enforces — enforcement is always on). Profiles make the
harness contribution measurable: run the same task and flow under each
profile and compare episode packages.

| Artifact exposed to the agent | H0 | H1 | H2 | H3 |
|---|---|---|---|---|
| task description, repository | ✓ | ✓ | ✓ | ✓ |
| tool registry + test-command registry (rendered from `tools`/`allow.shell`) | — | ✓ | ✓ | ✓ |
| tool-usage protocol (system prompt section) | — | ✓ | ✓ | ✓ |
| project memory files (`memory` declarations) materialised in workdir | — | — | ✓ | ✓ |
| `TASK_STATE.md` materialised and re-read each turn | — | — | ✓ | ✓ |
| context-selection protocol (system prompt section) | — | — | ✓ | ✓ |
| deterministic check registry (rendered from `ensure`) | — | — | — | ✓ |
| bug-reproduction, attribution, verification protocols (system prompt sections) | — | — | — | ✓ |
| verification report template | — | — | — | ✓ |
| hidden evaluator notes | — | — | — | — |

Mechanics: `leash run --profile h2` or `[profile]` in `leash.toml`. The runtime
renders the artifacts into `<workdir>/harness/` before each `run` and appends
the protocol sections to the agent's system prompt (after the program's own
system prompt, so prompt caching still works). Which artifacts were rendered is
recorded in the manifest and in the context trace.

The visibility matrix is data (`std/profiles/*.toml`) so packages can define
their own ladders (e.g. `h2+entropy`).

## 4. Failure taxonomy

`FailureKind` is the paper's taxonomy plus runtime kinds:

| Kind | Meaning | Typical raiser |
|---|---|---|
| `Context` | missing/misused context; `require` false | program, `require` |
| `Tool` | tool missing/unstable/misused; command failed or timed out; driver transport error | runtime tool layer, drivers |
| `Feedback` | feedback unavailable or uninterpretable (e.g. test runner output unparsable) | `test`, program |
| `Verify` | requirements not proven | `verify`, `complete` |
| `Recovery` | recovery loop exhausted without progress | `retry`, patterns |
| `Entropy` | maintenance burden introduced | audit, review patterns |
| `Model` | model output invalid after retries; reasoning failure despite adequate harness | `ask`/`run` validation, program |
| `Budget` | budget exhausted | runtime |
| `Permission` | capability violation or declined gate | runtime, program |
| `Replay` | journal divergence | runtime |
| `Unknown` | unattributed | anything |

`attempt/recover` on kinds lets programs encode "attribution before recovery":
recover `Context` by widening `select`, `Tool` by switching commands or
retrying with backoff, `Verify` by re-attributing, `Budget` by degrading.

## 5. Memory in v1 and later

v1 supports: working memory (the `run` transcript, managed by the driver with
context editing / compaction where available), project memory (`memory`
resources), task state (`state`). Deferred: experiential memory / skill
libraries (promote successful `tool`s or flows into a package), multi-agent
shared memory (see `docs/08-multi-agent.md`). All memory use must be traced;
`select(memory:)` is the only way project memory reaches a model, so the
context trace is complete by construction.

## 6. Harness evolution (SIA, Meta-Harness, AHE)

Leash programs are data (LIR) and every run yields telemetry, so a meta-agent
loop is expressible in Leash itself:

```
loop (generation g):
  run flow_g on task set → episode packages
  feedback agent: ask(analyse packages) -> ChangeContract { component, target_failure_mode, predicted_gain,
                                                         invariants, falsifier, rollback }
  apply proposed LIR/source edit → flow_{g+1}
  evaluate flow_{g+1} on held-out tasks with replay/reuse; promote only if non-regressing
```

The `ChangeContract` type and the `leash evolve` command are roadmap items
(`docs/12-roadmap.md`, M10). What v1 must get right so this is possible later:
stable LIR, journals that replay, `leash test` with reuse, and task contracts
(`ensure`) that stay fixed while flows change — the verifier is the anchor that
SIA-style loops optimise against.
