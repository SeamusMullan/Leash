# WP-18: Claude Code driver (headless CLI)

Milestone: M5 · Size: M · Status: see `docs/status.md`

## Goal
`drivers.claude_code(...)` runs `ask` and `run` through the Claude Code CLI in
print mode. The runtime translates the active capability set to CLI flags,
exposes Leash and MCP tools through the per-run MCP server, parses the
`stream-json` event stream into child journal entries and the tool trace,
accounts cost and usage, and post-checks the working-copy diff. The same
`examples/02_fix_bug.leash` that runs with the API driver runs unchanged with
this driver.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/09-drivers-and-tools.md` §1 (contract, common rules), §3 (flag mapping, limitations)
- `docs/07-budgets-and-capabilities.md` §2.3 (translation table, post-hoc diff check), §5 (what cannot be enforced)
- `docs/03-runtime-architecture.md` §8.1 (per-run MCP server), §9
- `docs/04-determinism-and-replay.md` §3, §5 (driver notes, cancellation status)
- `docs/adr/0004-drivers.md`
- WP-13 (DriverEvent/EventSink, budget accounting), WP-14 (`leash mcp-serve`, MCP config), WP-12

## Scope
- Verify every flag against `claude --help` on the installed version before
  use and keep the mapping in one table (`claude_code::flags`). Flags known
  from Claude Code 2.1.269: `-p/--print`, `--output-format stream-json`,
  `--input-format`, `--json-schema <schema>`, `--model`, `--effort`,
  `--permission-mode <acceptEdits|auto|bypassPermissions|manual|dontAsk|plan>`,
  `--permission-prompts <target>`, `--allowedTools`, `--disallowedTools`,
  `--tools`, `--max-budget-usd`, `--mcp-config`, `--strict-mcp-config`,
  `--append-system-prompt`, `--system-prompt`, `--session-id`, `--resume`,
  `--add-dir`, `--settings`, `--agents`, `--fallback-model`.
- `ask`: `claude -p <prompt> --output-format json --tools ""` (no tools; a
  single turn), `--json-schema` when a schema is present; the `result` field
  (or the structured-output field, name verified at implementation time)
  becomes `AskResponse`.
- `run`: `claude -p <task+context> --output-format stream-json` in the run
  working copy with: `--model`, `--effort`; `--append-system-prompt` (program
  system prompt + profile protocol sections from WP-17); `--json-schema` when
  a final schema is present; `--max-budget-usd <remaining cost>`;
  `--session-id <uuid>` (journaled; `--resume` for `retries` continuation);
  `--mcp-config <run>/mcp.json --strict-mcp-config` pointing at `leash mcp-serve --run <id>`;
  `--settings` with a minimal settings JSON so user/project settings do not leak;
  `--add-dir` for read/write roots outside the working copy.
- Capability translation: `fs.read` → `Read`, `Glob`, `Grep` allowed;
  `fs.write` → `Edit`, `Write` (plus `MultiEdit`/`NotebookEdit` if present);
  each `allow.shell` pattern → `Bash(<pattern>)`; `net` empty →
  `--disallowedTools WebFetch WebSearch`; everything not allowed is listed in
  `--disallowedTools`; `--permission-mode dontAsk` by default so denied tools
  fail instead of prompting; investigate `--permission-prompts` to route
  prompts to the runtime's gate policy and use it if the stream-json input
  protocol supports answering them.
- Event parsing (record a real stream first; do not rely on memory):
  `system/init` (session id, tools, model), `assistant` messages (content
  blocks incl. `tool_use`), `user` messages with `tool_result`, `result`
  (`subtype`, `result` text, structured output, `usage`, `total_cost_usd`,
  `num_turns`, `duration_ms`, `session_id`). Map to `DriverEvent`; each
  `tool_use`/`tool_result` pair becomes a child `tool` journal entry (input,
  output, duration, `is_error`), each assistant message a `llm.turn` child.
- Usage and cost: per-message `usage` where present; the final
  `total_cost_usd` reconciled with the price table (record both; charge the
  CLI-reported cost when present).
- Budget enforcement mid-run: count assistant turns against `Budget.turns`
  and `max_turns`; cancel when exceeded or when the wall deadline passes.
- Cancellation: SIGTERM, 10 s grace, SIGKILL; journal `cancelled`.
- Environment: scrubbed to the `env` allowlist plus the variables Claude Code
  needs to authenticate (`ANTHROPIC_API_KEY` or its OAuth store via `HOME`);
  document which are passed and why.
- Notes for unsupported options (`temperature`, host allowlists) in the
  journal entry.
- Post-hoc diff check after every `run` (docs/07 §2.3 point 4): revert and
  journal a `Permission` violation for changes outside `fs.write` globs.
- `leash doctor` probe: `claude --version`, `claude --help` flag presence,
  cached in `.leash/doctor.json` with the version as key.

## Out of scope
- The Claude Agent SDK (TypeScript/Python) as a backend.
- Interactive (non-print) sessions, IDE integrations, hooks configuration.
- Hard network isolation of the CLI (container backend, M10).
- Codex (WP-19), Managed Agents (M10).

## Interfaces
```rust
pub struct ClaudeCodeDriver { pub model: Option<String>, pub effort: Option<Effort>, pub permission_mode: PermissionMode,
                              pub extra_flags: Vec<String>, pub binary: PathBuf /* default "claude" */ }
impl Driver for ClaudeCodeDriver { /* ask, run, caps() with structured_output=true, tools=true, sessions=true, cost_reporting=true */ }

pub mod flags { pub fn caps_to_tool_flags(caps: &CapSet, available_tools: &[String]) -> (Vec<String> /*allowed*/, Vec<String> /*disallowed*/); }
pub mod events { pub enum ClaudeEvent { Init {..}, Assistant {..}, User {..}, Result {..}, Unknown(serde_json::Value) }
                 pub fn parse_line(line: &str) -> Result<ClaudeEvent, ParseError>; }
// shared with WP-19 (create here, WP-19 extracts if it lands first):
pub mod subprocess { pub struct JsonlChild; pub async fn spawn_jsonl(cmd: Command, env: EnvPolicy) -> Result<JsonlChild, DriverError>;
                     pub fn scrub_env(allow: &[String], extra: &[(&str, String)]) -> HashMap<String, String>;
                     pub async fn cancel(child: &mut JsonlChild, grace: Duration); }
```
Per-run files: `<run>/mcp.json`, `<run>/claude-settings.json`.

## Deliverables
- `crates/leash-drivers/src/claude_code/{mod.rs, flags.rs, events.rs, run.rs}`, `crates/leash-drivers/src/subprocess.rs`
- Fixtures: `crates/leash-drivers/tests/fixtures/claude_code/*.jsonl` (recorded streams: init, a
  three-tool-call run, a denied tool, a structured result, a `max_budget` stop)
- Tests: event parsing per fixture; flag mapping table tests; a fake `claude`
  shell script in `tests/support/fake_claude.sh` that replays a fixture, used
  for end-to-end driver tests without the real CLI; `#[ignore]` live test.
- `leash doctor` section for Claude Code.
- Docs: `docs/09-drivers-and-tools.md` §3 corrected to the verified flag set and event schema.

## Acceptance criteria
- `cargo nextest run -p leash-drivers` passes using the fake CLI; the
  end-to-end test asserts three child `tool` entries, one `llm.run` parent
  with `usage` and `cost_usd`, and that a fixture edit outside `fs.write`
  globs is reverted and journaled as `Permission`.
- Flag mapping: `allow { fs.read: "**", fs.write: ["src/**"], shell: ["npm test*"], net: [] }`
  yields `--allowedTools Read Glob Grep Edit Write "Bash(npm test*)"` and
  `--disallowedTools WebFetch WebSearch …` (exact list from the test).
- `LEASH_LIVE=1 cargo nextest run -p leash-drivers --run-ignored all claude_code_live`
  runs `examples/02_fix_bug.leash` on `tests/fixtures/repoA` with the real CLI
  and reaches `complete` (label may be `autonomous_verified_success` or
  `unverified_success` depending on the model; the test asserts the journal
  shape, not the label).
- `leash doctor` reports the CLI version and any missing flag by name.
- Ctrl-C during a live run leaves a journal entry with `status: cancelled`
  and `leash resume` restarts the `run` from scratch (a `run` is atomic).

## Notes and pitfalls
- The CLI's print mode reads user and project settings unless told not to;
  always pass `--settings` and `--strict-mcp-config`. Check whether
  `--setting-sources` exists in the installed version and prefer it.
- `--max-turns` was not visible in the 2.1.269 help output; if absent, turn
  limits are enforced by counting assistant events and cancelling.
- The `result` event's structured-output field name must be verified
  (`structured_output` is the expected name); record a fixture with
  `--json-schema` before writing the parser.
- Cost: `total_cost_usd` is authoritative for the CLI; also store computed
  cost so eval metrics are comparable across drivers.
- A `run` is one journal effect; the CLI session cannot be replayed
  step-by-step. Child entries exist for traces and budgets only.
- Never pass `--dangerously-skip-permissions`; `bypassPermissions` mode is
  only allowed when `allow` equals `caps.full_access` and the gate policy
  approved at run start.

## Dependencies
Hard: WP-13 (event/budget plumbing), WP-14 (MCP server). Soft: WP-17 (profile sections).

## Estimated size
M (2–4 days).
