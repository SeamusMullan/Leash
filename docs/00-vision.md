# Leash — Vision and Principles

Status: draft v0.1 (2026-09-12). Owner: Sarah. Architect notes by Claude.

## One-line

Leash is a programming language for harnessing coding agents. Leash code decides
**control flow, budgets, permissions and verification**. Models decide **content**.
Every model or tool interaction is a typed, journaled effect, so a Leash run is
deterministic, replayable, resumable and auditable.

## The problem

Agent frameworks today put the harness inside prompts and ad-hoc glue code. The
loop that decides what happens next is either the model itself (ReAct-style loops)
or a pile of Python that nobody can replay. The result is well known:

- Runs are not reproducible. The same task on the same repo produces different
  paths, costs and outcomes. Debugging means reading a transcript.
- Completion is an assertion. The agent says "done"; a human verifies.
- Budgets, permissions and context selection are implicit. They live in scattered
  configuration or not at all.
- Failures are not attributed. A failed run is blamed on "the model" even when the
  missing piece was context, a tool, feedback, or verification.
- Harnesses cannot be evaluated or evolved, because they are not a first-class,
  inspectable artifact.

The three reference papers listed in `reference_paper/README.md` converge on the
same diagnosis:
capability is a property of the **model–harness–environment system**, and the
harness is the under-engineered part. See `docs/research/paper-notes.md`.

## The thesis

> A harness is a program. It should be written in a language built for it.

Leash makes the harness explicit, typed and executable:

1. **Code decides, models propose.** Control flow (`if`, `for`, `retry`,
   `parallel`, `attempt/recover`) is ordinary deterministic code. Model calls
   (`ask`, `run`) return typed values validated against schemas derived from Leash
   types. A model never chooses the next step of the program; it only fills in
   values the program asked for.
2. **Effects are tracked.** The type system tracks which functions perform effects
   (`llm`, `tool`, `shell`, `fs`, `net`, `spawn`, `human`, `clock`, `rand`).
   Pure code is provably deterministic. Effectful code is journaled.
3. **Runs are event-sourced.** Every effect result is appended to a journal.
   Replaying a journal reproduces the run exactly without calling any model. A run
   can be resumed after a crash, forked at a checkpoint, diffed against another
   run, or reused as a cache.
4. **Completion is evidence.** A `task` declares `ensure` checks. A flow cannot
   `complete` until each check has evidence produced by the runtime (deterministic
   probes, tests, lint). "Verified" is a runtime property, not a claim.
5. **Resources are first-class.** Budgets (tokens, money, wall time, tool calls)
   and capabilities (file globs, shell allowlists, network hosts) are lexically
   scoped values. The runtime enforces them at every effect boundary.
6. **Failures have kinds.** Errors carry a failure taxonomy (`Context`, `Tool`,
   `Feedback`, `Verify`, `Recovery`, `Entropy`, `Model`, `Budget`, `Permission`,
   `Unknown`) so that programs can recover by kind and runs can be attributed.
7. **Every run produces an episode package.** Journal, traces (action, tool,
   context, verification, attribution, intervention, entropy, outcome), artifacts
   and a verification report. This is the unit of evaluation and of learning.
8. **Programs are data.** Leash compiles to a canonical JSON intermediate
   representation (LIR). Tooling, evaluators and meta-agents can read and rewrite
   harnesses without parsing source text.
9. **Driver-agnostic.** The same program runs against the Anthropic Messages API,
   Claude Code (headless), Codex CLI, Managed Agents or a mock driver used for
   tests. The driver is configuration, not code.

## Goals for v1

- A small, readable language with static types, effect tracking and capability
  checking, compiled and run by a single `leash` binary.
- Deterministic replay and resume of any run from its journal.
- Typed structured outputs from every supported driver.
- Task contracts with `require`, `ensure`, `budget`, `allow`, and evidence-bound
  `complete`.
- Sandboxed built-in tools (`fs`, `shell`, `git`, `test`) plus MCP tool import.
- Deterministic fork/join concurrency (`parallel`, `race`, `map`) with journaled
  results.
- An episode package format and a `leash report` command.
- A standard library of harness patterns: reproduce → attribute → fix → verify →
  report; review; triage; debate; entropy audit.
- Harness profiles H0–H3 so that the contribution of each runtime support can be
  measured, as in the AI Harness Engineering paper.

## Non-goals for v1

- Training or fine-tuning models (SIA-style weight updates). Leash exposes the
  verifier and the harness-as-data so that such loops can be built on top later.
- A general-purpose language. Leash has no classes, no macros, no user-defined
  effects in v1, and a deliberately small standard library.
- A hosted service. Leash is a local CLI first.
- GUI/browser automation, embodied agents, multimodal inputs. The effect model can
  grow to cover them; v1 targets software engineering tasks.
- Being the agent. Leash orchestrates agents; the inner agentic loop belongs to the
  driver (Claude Code, Codex, the Anthropic tool runner) or to a Leash `run` with
  a tool loop provided by the runtime.

## Design principles (tie-breakers)

When two designs compete, prefer the one that:

1. keeps the deterministic/effectful boundary visible in source;
2. produces better evidence in the episode package;
3. can be replayed from the journal without a model;
4. is enforced by the runtime rather than requested of the model;
5. is smaller.

## What success looks like

- A developer writes a 60-line `.leash` file that fixes a class of bugs across a
  repository, runs it overnight against 200 issues with a fixed budget, and in the
  morning reviews a table of verified, unverified and failed episodes with
  attributions.
- A researcher runs the same task under profiles H0–H3 and three models, and reads
  the per-cell metrics (AVSR, M-HIR, verification autonomy, entropy delta)
  straight from episode packages.
- A team commits Leash programs next to their code. CI runs `leash test` with the
  mock driver and replays golden journals. Harness changes are reviewed as diffs.

## Naming

The name is the metaphor. A leash does not steer the dog; it bounds where the dog
can go. Leash programs bound where models can go: which files, which commands,
how much money, and what counts as done.
