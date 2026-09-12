# WP-19: Codex driver (headless CLI)

Milestone: M5 · Size: S · Status: see `docs/status.md`

## Goal
`drivers.codex(...)` runs `ask` and `run` through `codex exec`, reusing the
subprocess infrastructure of the Claude Code driver. Capability sets map to
Codex sandbox modes, JSONL events become child journal entries, the final
message is typed through `--output-schema`, and the post-hoc diff check
applies. This proves the driver abstraction against a second, independently
designed agent CLI.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/09-drivers-and-tools.md` §1, §4
- `docs/07-budgets-and-capabilities.md` §2.3 (Codex column), §5
- `docs/adr/0004-drivers.md`
- WP-18 (shared `subprocess` module, post-hoc diff check, doctor probe pattern)
- WP-14 (`leash mcp-serve`, MCP config for Codex via `-c`)

## Scope
- Verify flags against `codex exec --help` on the installed version (0.154.0
  at the time of writing) and keep them in `codex::flags`. Known: `codex exec
  [PROMPT]`, `--json`, `--output-schema <FILE>`, `-o/--output-last-message <FILE>`,
  `-s/--sandbox <read-only|workspace-write|danger-full-access>`, `-C/--cd <DIR>`,
  `--add-dir <DIR>`, `-m/--model`, `-p/--profile`, `-c key=value`, `--ephemeral`,
  `--skip-git-repo-check`, `--approve-for-me`, `resume`/`fork` subcommands.
- `ask`: `codex exec --json --ephemeral -s read-only -C <tmpdir> [-m model]
  [--output-schema file] <prompt>`; the last agent message (from `-o` or the
  events) becomes `AskResponse`; structured output parsed from it.
- `run`: as above in the run working copy with `-s read-only` when the caps
  have no `fs.write`, `-s workspace-write` otherwise, `--add-dir` for extra
  writable roots, `-c approval_policy="never"` (verify key) so the run never
  blocks on prompts, `-c mcp_servers.leash.command=...` / `args` for the
  per-run MCP server, `--output-schema` for the final answer, `-o` as a
  fallback source of the final message.
- `danger-full-access` only when `allow` equals `caps.full_access` and the
  gate policy approved at run start; otherwise never.
- Event parsing (record real output first): thread/turn lifecycle events,
  item events for agent messages, command executions (command, output, exit
  code), file changes, reasoning summaries, and the final turn event with
  usage. Map to `DriverEvent`; command executions and file changes become
  child `tool` entries; usage charged to the budget scope.
- Extract the shared module `leash_drivers::subprocess` from WP-18 if it did
  not already: `spawn_jsonl`, `scrub_env`, `cancel`, a line reader that
  tolerates non-JSON lines (logged, not fatal).
- Environment: scrubbed to the `env` allowlist plus what Codex needs to
  authenticate (`OPENAI_API_KEY` or `CODEX_HOME` auth store); `CODEX_HOME`
  pointed at `<run>/codex-home` with a generated minimal `config.toml` so user
  config does not leak (`--ignore-user-config` if present).
- Notes for unsupported options (`effort` if not mappable, host allowlists).
- Post-hoc diff check (shared with WP-18).
- `leash doctor` section: `codex --version`, flag presence.

## Out of scope
- Codex cloud/web tasks, `codex review`, `codex resume` (beyond noting the
  thread id in the journal).
- OpenAI API driver (`--oss`/local providers untouched; pass-through via `-c` only).
- Anything about model pricing for OpenAI models beyond a price-table entry
  with `unknown` cost (tokens counted, cost `null`).

## Interfaces
```rust
pub struct CodexDriver { pub model: Option<String>, pub profile: Option<String>, pub sandbox_override: Option<SandboxMode>,
                         pub extra_config: Vec<(String, String)>, pub binary: PathBuf /* default "codex" */ }
impl Driver for CodexDriver { /* caps(): structured_output=true, tools=true, sessions=false, cost_reporting=false */ }
pub mod events { pub enum CodexEvent { ThreadStarted{..}, TurnStarted, ItemCompleted(Item), TurnCompleted{ usage: Option<Usage> }, Unknown(serde_json::Value) }
                 pub fn parse_line(line: &str) -> Result<CodexEvent, ParseError>; }
pub fn sandbox_for(caps: &CapSet) -> SandboxMode;   // read-only | workspace-write | (full only via explicit policy)
```
Per-run files: `<run>/codex-home/config.toml`, `<run>/codex-schema.json`, `<run>/codex-last.txt`.

## Deliverables
- `crates/leash-drivers/src/codex/{mod.rs, flags.rs, events.rs, run.rs}`
- Fixtures: `crates/leash-drivers/tests/fixtures/codex/*.jsonl` (recorded: a run with two
  command executions and one file change; a structured final message; a sandbox denial)
- `tests/support/fake_codex.sh` replaying fixtures; `#[ignore]` live test.
- Docs: `docs/09-drivers-and-tools.md` §4 corrected to the verified flags and event names.

## Acceptance criteria
- `cargo nextest run -p leash-drivers` passes with the fake CLI: the run
  test asserts two child `tool` entries of kind `shell` and one of kind
  `fs.write`, usage charged, structured final answer parsed, and an
  out-of-scope file change reverted and journaled as `Permission`.
- `sandbox_for` unit tests: no writes → `read-only`; writes → `workspace-write`;
  full access without gate approval → error, never `danger-full-access`.
- `LEASH_LIVE=1 cargo nextest run -p leash-drivers --run-ignored all codex_live`
  runs `examples/02_fix_bug.leash` on `tests/fixtures/repoA` with the real
  CLI to `complete` (journal shape asserted, not the label).
- `leash doctor` reports the Codex version and missing flags by name.
- The `subprocess` module has no Claude- or Codex-specific code and both
  drivers use it (a grep in CI enforces `use crate::subprocess`).

## Notes and pitfalls
- Codex `--json` event names change between releases; the parser must keep
  unknown events as `Unknown` and continue, with a summary note in the
  journal entry listing unknown event types seen.
- `-c` values are parsed as TOML; quote strings (`-c 'approval_policy="never"'`).
- Codex may print non-JSON lines (warnings) to stdout; the reader must skip
  them without failing.
- Effort has no direct flag; map to `-c model_reasoning_effort="..."` only
  if the installed version documents it, otherwise note the omission.
- The working copy must be a git repository for `workspace-write`
  sandboxing to behave; WP-11 worktrees satisfy this; `--skip-git-repo-check`
  is only for `ask` in a temp dir.

## Dependencies
Hard: WP-14. Soft: WP-18 (shared subprocess module; whichever lands first creates it).

## Estimated size
S (≤ 1 day, after WP-18).
