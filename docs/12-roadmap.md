# Roadmap and Work Packages

Status: draft v0.1 (2026-09-12). Milestones are vertical slices where possible:
M1 produces a runnable, replayable program end-to-end before the language is
complete. Each work package (WP) has a brief in `docs/subagents/`. Track status
in `docs/status.md`.

## Milestones

| # | Milestone | Exit criterion |
|---|---|---|
| M0 | Foundations | workspace builds, CI green, machine-readable spec files exist |
| M1 | Tracer bullet | `leash run examples/01_hello.leash` works against the Anthropic API and the mock driver; `leash replay` reproduces it with no network |
| M2 | Language core | all `examples/*.leash` parse, format round-trip, type-check and lower to LIR |
| M3 | Effects, tools, sandbox | `run` with the runtime-owned tool loop against a sandboxed worktree; budgets and capabilities enforced |
| M4 | Harness constructs | `examples/02_fix_bug.leash` completes with an episode package and report on the repoA fixture |
| M5 | CLI drivers | the same example runs with `claude_code` and `codex` drivers |
| M6 | Concurrency | `examples/03` and `04` run; replay of parallel runs is deterministic; chaos test passes |
| M7 | Stdlib and patterns | patterns listed in `docs/08-multi-agent.md` §3 implemented and tested |
| M8 | Tooling | LSP, VS Code extension, `leash test`, docs site |
| M9 | Evaluation | `leash eval` runs the ladder suite over profiles × models and emits metrics |
| M10 | Beyond v1 | managed agents driver, container sandbox, `leash evolve`, transactional shared state, sessions |

## Work packages

Dependencies are hard unless marked (soft). Estimates are for one focused
implementer (agent or human) and are rough.

| WP | Title | Milestone | Depends on | Size |
|---|---|---|---|---|
| WP-01 | Repository scaffold, workspace, CI | M0 | — | S |
| WP-02 | Machine-readable spec: LIR, journal, trace schemas, profiles, prices | M0 | — | M |
| WP-03 | Lexer, parser, AST, formatter (v0 subset) | M1 | 01 | M |
| WP-04 | Interpreter core, journal, replay/resume (v0 subset) | M1 | 02, 03 | L |
| WP-05 | Mock driver and Anthropic `ask` driver | M1 | 02 | M |
| WP-06 | Run directory, manifest, CAS, trace layer v0, report skeleton | M1 | 02, 04 (soft) | M |
| WP-07 | Full parser and formatter (complete grammar) | M2 | 03 | M |
| WP-08 | Type checker and JSON Schema generation | M2 | 07 | L |
| WP-09 | Effect inference, capability static checks, diagnostics | M2 | 08 | M |
| WP-10 | LIR: model, lowering, stable ids, JSON | M2 | 08, 02 | M |
| WP-11 | Sandbox and built-in tools (fs, shell, test, git), snapshots | M3 | 04 | L |
| WP-12 | Budget and capability runtime enforcement | M3 | 04, 11 | M |
| WP-13 | Anthropic `run` driver with runtime tool loop, caching, task budgets | M3 | 05, 11, 12 | L |
| WP-14 | MCP client, Leash `tool` declarations, per-run MCP server | M3 | 11, 12 | M |
| WP-15 | Tasks, flows, checks, evidence, `complete`, report, outcome labels | M4 | 10, 11, 12 | L |
| WP-16 | Failures, attempt/recover, retry, state, checkpoints, fork | M4 | 04, 10, 11 | M |
| WP-17 | Context selection, memory, profiles H0–H3, context trace | M4 | 11, 13 | M |
| WP-18 | Claude Code driver | M5 | 13, 14 | M |
| WP-19 | Codex driver | M5 | 18 (soft: shares subprocess infra) | S |
| WP-20 | Scheduler: parallel, race, map, spawn/await, cancellation | M6 | 04, 12, 16 | L |
| WP-21 | Standard library and patterns, roles, caps presets, entropy audit | M7 | 15, 16, 20 | L |
| WP-22 | CLI completeness and UX | M8 | 15, 16, 17 | M |
| WP-23 | LSP and VS Code extension | M8 | 09 | M |
| WP-24 | Documentation site and tutorials | M8 | 21, 22 | M |
| WP-25 | Test infrastructure: `leash test`, golden journals, chaos, properties | M8 | 05, 16, 20 | M |
| WP-26 | `leash eval`: ladder suite, metrics, fixtures, SWE-bench adapter (optional) | M9 | 15, 17, 22 | L |

Critical path: 01 → 03 → 04 → 11 → 12 → 13 → 15 → 17 → 18 → 26.
Parallel tracks after M1: {07, 08, 09, 10} (language), {11, 12, 13, 14}
(runtime), {06, 25} (infra), {23, 24} once 09 lands.

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| Rust slows iteration for agents implementing WPs | small crates, tree-walking interpreter first, no premature optimisation, `cargo nextest`, `just` recipes |
| CLI driver flags drift (Claude Code, Codex release weekly) | driver capability probing in `leash doctor`; flags table in one module; integration tests marked `#[ignore]` unless CLIs present |
| Structured-output differences across drivers | runtime validation + re-ask is the universal fallback; drivers only accelerate |
| Journal format churn | `journal_version` from day one; migration tool in WP-06; golden journals in CI |
| Capability enforcement gaps with subprocess drivers | documented limits (`docs/07` §5); post-hoc diff revert; container backend in M10 |
| Grammar ambiguity discovered late | WP-03/07 must add every ambiguity to `spec/grammar.ebnf` notes; parser fuzz tests |
| Scope creep in the stdlib | patterns must each fit in ~60 lines of Leash; anything larger becomes an example, not std |
| Cost of running evaluations | mock driver, `--reuse`, small fixture repos, budgets on every suite |

## Decisions still open for Sarah

None. All four are settled:

Settled: Rust is confirmed (`docs/adr/0001-implementation-language.md`).
Settled: `--strict` is off by default and required for `std` (`docs/02` §5.3).
Settled: the licence is Apache-2.0 (`docs/adr/0010-licensing.md`). The
repository is https://github.com/SeamusMullan/Leash, hosted on GitHub with
GitHub Actions for CI.
