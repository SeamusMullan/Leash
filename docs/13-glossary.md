# Glossary

- **Agent** — a value `Agent { driver, system, tools, budget, allow }`. Not a
  process; a configuration the runtime uses when the program says `ask`/`run`.
- **Ask** — one typed model turn without tools.
- **Attribution** — a structured diagnosis of a failure (observed, expected,
  kind, evidence, alternatives, next action) recorded before recovery.
- **Budget** — limits on tokens, cost, wall time, tool calls, model calls,
  turns; lexically scoped; enforced at effect boundaries.
- **Capability set (CapSet)** — what effects may touch: file globs, shell
  patterns, hosts, human gates, spawn, env, secrets.
- **CAS** — content-addressed store for large journal payloads (`.leash/cas`).
- **Check** — a requirement bound to a method (`shell`, `test`, fn, `judge`)
  and a predicate; declared in `ensure`.
- **Checkpoint** — a named point in a flow where state and the working copy
  are snapshotted; the target of `fork`.
- **Complete** — the terminal expression of a flow; succeeds only with fresh
  passing evidence for every check.
- **Context** — the selected, sorted, hashed bundle of files and memory that
  a model sees.
- **Driver** — the adapter that performs `ask`/`run` against a backend
  (Anthropic API, Claude Code, Codex, Managed Agents, mock).
- **Effect** — any interaction with the world: `llm tool shell fs.read
  fs.write net spawn human clock rand env`. Every effect is journaled.
- **Effect id / path** — the stable identity of an effect occurrence, derived
  from the program structure and iteration/branch indices, never from time.
- **Episode / episode package** — one run of a flow and its directory of
  journal, traces, artifacts and report.
- **Evidence** — the recorded result of a check: pass/fail/timeout/skipped,
  deterministic or not, with scope and confidence.
- **Failure / FailureKind** — the error value and its taxonomy (`Context Tool
  Feedback Verify Recovery Entropy Model Budget Permission Replay Unknown`).
- **Flow** — the implementation of a task; a function that must end in
  `complete` or `fail`.
- **Fork** — a new run started from a checkpoint of an earlier run.
- **Gate** — a human approval effect.
- **Harness** — the runtime substrate around a model: context, tools, memory,
  state, observability, attribution, verification, permissions, entropy,
  interventions. A Leash program is an executable harness.
- **Journal** — the append-only log of effects; the source of truth for
  replay and traces.
- **LIR** — Leash intermediate representation; typed, desugared, JSON-serialisable.
- **Memory** — agent-readable project knowledge (`memory x = file(...)`).
- **Profile (H0–H3)** — which harness supports are exposed to the model.
- **Replay / resume** — re-execution from the journal without the world /
  with continuation.
- **Run** — an agentic session with tools, or the CLI command that starts an
  episode.
- **Sandbox** — the per-run working copy plus process, path, env and network
  policy.
- **State** — the typed mutable task-state record of a flow.
- **Task** — the contract: input, output, require, ensure, budget, allow.
- **Tool** — a typed callable exposed to models: built-in, Leash-defined, or MCP.
- **Trace** — a derived view of the journal (eight kinds).
- **Verify** — run checks and record evidence.
