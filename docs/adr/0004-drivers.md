# ADR-0004 Drivers as configuration; runtime owns the tool loop where possible

Status: accepted. Revised 2026-10-19: the Hermes driver is added, and the
driver build order is set to Hermes, then Claude Code.

## Context
Agents can be reached through the Messages API, Claude Code, Codex, Managed
Agents and others. Each has different structured-output, tool and sandbox
features. Hermes Agent (Nous Research) is the harness Sarah runs daily and
exposes a supported one-shot interface (`hermes -z`).

## Decision
A `Driver` trait with `ask` and `run`. For the API and mock drivers the
runtime owns the tool loop, so capabilities and budgets are enforced by
construction. For CLI drivers the runtime delegates the loop but (1) translates
capabilities to CLI flags, (2) exposes Leash and MCP tools through a per-run
MCP server so custom tools work uniformly, (3) parses event streams into child
journal entries, and (4) post-checks the diff against `fs.write`. Programs
never reference driver specifics beyond `std.drivers.*` constructors.

### Driver priority (2026-10-19)

| Order | Driver | Kind | Why |
|---|---|---|---|
| 1 | mock | in-process | CI and fixture path; always available |
| 2 | hermes | CLI | Sarah's daily harness; the simplest subprocess surface |
| 3 | claude_code | CLI | Richest CLI enforcement: event stream, per-run MCP, allowlists |
| 4 | anthropic | API | Precise enforcement and the runtime-owned tool loop; follows the CLI pair |
| 5 | codex | CLI | Second CLI backend; shares subprocess infrastructure |
| 6 | managed_agents | API | Latest; optional |

Priority means build order, and the order in which examples and docs present
drivers. It does not change the contract.

### `hermes` driver (v1)

Spawns `hermes -z <task>` with `--usage-file <path>` (JSON cost, tokens, model,
api_calls), `--in <workdir>`, `-m`, `--provider`, `--reasoning`,
`-t <toolsets>`, `--ignore-user-config`, `--ignore-rules`, `--worktree` when
isolation is requested, and `-r <session>` to resume.

- stdout carries the final text only. v1 emits no `DriverEvent::ToolCall` or
  turn events. Child journal entries are the final response, the usage report
  and the post-check diff.
- One-shot mode auto-bypasses approvals. Leash enforces through toolset
  restriction, a scrubbed environment, the working copy and post-checks
  against `fs.write`. The runtime never delegates a budget or capability to
  Hermes.
- Hermes has no per-run MCP configuration flag, and `hermes mcp add` edits
  persistent config. Leash `tool` declarations are unavailable through this
  driver in v1; the journal records the gap.
- `--pass-session-id` writes the id into the system prompt, not stdout. The
  driver reads it from `hermes sessions list` after the run and journals it.
- The driver id includes `hermes --version`.
- A later revision can drive `hermes acp` (JSON-RPC over stdio) for events and
  permission prompts.

## Alternatives
Only API drivers (loses the value of mature agent CLIs); only CLI drivers
(loses precise enforcement and cheap `ask`); the Claude Agent SDK as the sole
backend (no C++ SDK; would couple Leash to one vendor's harness).

## Consequences
- Uniform program semantics across drivers, with documented enforcement gaps
  for CLI drivers.
- Driver capability probing (`leash doctor`) and notes in the journal when
  options are ignored.
- Hermes proves the language against a real harness first. Claude Code
  follows with the enforcement-rich path.
- CLI drivers do not depend on the Anthropic `run` driver. The shared pieces
  (Driver interface and usage accounting) belong to the runtime packages; the
  roadmap dependency of WP-18 on WP-13 is build order, not a technical need.
- Roadmap impact: M1 runs mock plus Hermes; Claude Code follows; the Anthropic
  `ask` and `run` drivers move behind them. A Hermes driver work package is
  required.
