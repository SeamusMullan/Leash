# Drivers and Tools

Status: draft v0.1. Verified against Claude Code 2.1.269 `--help`, Codex CLI
0.154.0 `codex exec --help`, and the Claude API reference (models and request
shapes as of 2026-06). Re-verify flags before implementing; CLIs move.

## 1. Driver contract

```rust
#[async_trait]
pub trait Driver: Send + Sync {
    fn id(&self) -> DriverId;                       // name + model + config hash + tool version
    fn caps(&self) -> DriverCaps;                   // structured_output, tools, effort, sessions, cost_reporting, cancellation
    async fn ask(&self, req: AskRequest, ctx: &DriverCtx) -> Result<AskResponse, DriverError>;
    async fn run(&self, req: RunRequest, ctx: &DriverCtx, sink: &EventSink) -> Result<RunResponse, DriverError>;
}

pub struct AskRequest { system: Vec<PromptBlock>, prompt: String, schema: Option<JsonSchema>, effort: Option<Effort>,
                        model: Option<String>, max_tokens: Option<u32>, context: Option<RenderedContext>, attachments: Vec<Blob> }
pub struct AskResponse { text: String, json: Option<serde_json::Value>, usage: Usage, stop_reason: StopReason, raw: Blob, notes: Vec<String> }

pub struct RunRequest  { system: Vec<PromptBlock>, task: String, schema: Option<JsonSchema>, tools: Vec<ToolSpec>, caps: CapSet,
                         budget: BudgetView, workdir: PathBuf, max_turns: Option<u32>, effort: Option<Effort>, model: Option<String>,
                         profile_artifacts: Vec<RenderedArtifact>, session: Option<SessionRef> }
pub struct RunResponse { final_text: String, json: Option<serde_json::Value>, usage: Usage, turns: u32, transcript: Blob,
                         session: Option<SessionRef>, notes: Vec<String> }
```

`DriverCtx` carries the run id, a cancellation token, a payload sink (large blobs go to the content-addressed store), the price table and the translated capability set. A driver never sees the runtime's budget or capability stacks; the LLM effect handler builds a `DriverCtx` from its `EffectCtx` and converts the reported `Usage` into budget consumption. See `docs/subagents/shared-interfaces.md`.

`EventSink` receives `DriverEvent::{Turn, ToolCall{name, input}, ToolResult{...}, Text, Thinking, Usage, Error}` in real time; the runtime turns tool events into child journal entries and tool-trace lines, and enforces budgets mid-run (cancelling when exceeded).

Common rules:
- A driver MUST NOT fail because an option is unsupported; it records a note.
- A driver MUST report usage per turn where the backend exposes it.
- A driver MUST honour cancellation within a grace period (default 10 s).
- A driver MUST be constructible from `leash.toml` and from `std.drivers.*`.
- Driver-level retries (429/5xx/network) are internal and bounded (default 3,
  exponential backoff); each attempt is noted; after that, `DriverError` →
  `Tool` failure with `data.driver`.

## 2. `anthropic` driver (Messages API, raw HTTPS)

Rust has no official SDK; the driver uses `reqwest` against
`POST https://api.anthropic.com/v1/messages` with headers `x-api-key`,
`anthropic-version: 2023-06-01`, and `anthropic-beta` when needed.

Request construction (order matters for prompt caching: tools → system → messages):
- `model`: default `claude-opus-5` (config `[drivers.anthropic] model`).
- `max_tokens`: default 16000 for `ask`, 64000 for `run` turns; streaming
  (SSE) is used for every request so long outputs do not hit HTTP timeouts.
- `thinking: {"type": "adaptive"}` on every current model; `output_config.effort`
  from `Effort` (`low|medium|high|xhigh|max`); default `high` for `ask`,
  `xhigh` for `run`.
- Structured output: `output_config.format = {"type": "json_schema", "schema": <schema>}`
  when a schema is present. The runtime still validates and applies refinements.
- Tools: each `ToolSpec` becomes `{name, description, input_schema, strict: true}`;
  the schema generator guarantees `additionalProperties: false` and full
  `required`. `tool_choice` stays `auto`. Parallel tool calls are executed
  concurrently by the runtime and returned in one user message.
- Prompt caching: one `cache_control` breakpoint on the last stable system
  block (program system prompt + profile protocol sections); the rendered
  `Context` goes into the first user message, sorted and stable, so repeated
  runs on the same context hit the cache.
- Task budgets: for `run`, `output_config.task_budget = {"type":"tokens","total": remaining_tokens}`
  with beta `task-budgets-2026-03-13` when the budget has a token limit.
- Fallbacks: when the model is `claude-fable-5-1`, include `fallbacks: "default"`
  with beta `server-side-fallback-2026-07-01` unless disabled in config; a
  `refusal` stop reason without fallback becomes `Model` failure with
  `data.stop_details`.
- Compaction: for long runs, beta `compact-2026-01-12` with the response
  content appended verbatim; enabled by `[drivers.anthropic] compaction = true`.
- Usage: `input_tokens`, `output_tokens`, `cache_read_input_tokens`,
  `cache_creation_input_tokens` per turn; cost from the price table.
- Auth: `ANTHROPIC_API_KEY`; otherwise `ANTHROPIC_AUTH_TOKEN`; the driver
  refuses to start without one and says which env var it looked at.

`run` loop: the runtime owns it. Turn → if `stop_reason == tool_use` execute
tools via the tool layer (capabilities enforced) → append results → repeat
until `end_turn`, `max_turns`, or budget. Message history is append-only (never
edit earlier turns; thinking blocks are passed back unchanged).

## 3. `claude_code` driver (headless CLI)

Spawns `claude` with:

```
claude -p <task>
  --output-format stream-json           # events parsed into DriverEvent
  --json-schema <schema>                # when a schema is present (structured final result)
  --model <model> --effort <level>
  --permission-mode <acceptEdits|dontAsk|plan|bypassPermissions>   # from run options; bypass only if allow is full_access
  --allowedTools "Read" "Edit" "Write" "Grep" "Glob" "Bash(npm test*)" "Bash(node harness/probe.js *)"
  --disallowedTools "WebFetch" "WebSearch"        # unless net allowed
  --max-budget-usd <remaining cost>
  --mcp-config <per-run mcp.json> --strict-mcp-config     # exposes Leash tools + allowed MCP tools
  --no-session-persistence              # unless the program asked to keep the session (resume)
  --permission-prompts <target>         # route remaining prompts to the runtime gate policy where supported
  --append-system-prompt <program system + profile sections>
  --session-id <uuid>                   # journaled; --resume for continuation
  --add-dir <extra roots from fs.read/fs.write>
```
cwd = the run working copy. Environment scrubbed except allowlisted vars and
`ANTHROPIC_API_KEY`/auth. stdout events → journal children (tool calls with
inputs/outputs), usage and `total_cost_usd` from the final `result` event.
Permission prompts: with `--permission-mode dontAsk` denied tools fail; the
runtime prefers explicit allowlists over interactive prompts. Structured
output: the `result` event carries the JSON matching `--json-schema`.

Limitations to record as notes: no `temperature`; no `--max-turns` in 2.1.269
(the runtime counts assistant turns in the event stream and cancels at the
`turns` budget); network not sandboxed by the CLI; the CLI reads user settings
unless `--settings`/`--strict-mcp-config` restrict it. Re-check `claude --help`
at implementation time; flags change between releases.

## 4. `codex` driver (headless CLI)

```
codex exec <task> --json --output-schema <file> -o <last-message-file>
  --sandbox <read-only|workspace-write>   # from CapSet: writes present → workspace-write
  --cd <workdir> --add-dir <extra writable roots>
  -m <model> --ephemeral --skip-git-repo-check
  -c <key=value> ...                       # approval policy, MCP servers, features
```
JSONL events on stdout → `DriverEvent`. `--output-schema` gives a typed final
message. `danger-full-access` is only used when `allow` is `full_access` and the
gate policy approves at run start.

## 5. `managed_agents` driver (post-v1)

Anthropic Managed Agents: create an agent once (`ant` CLI / API), start a
session per `run`, stream events, answer tool calls for Leash-defined tools.
Benefits: hosted sandbox, session budgets. Deferred until the local drivers are
solid. Reference: Claude API docs, Managed Agents (beta).

## 6. `mock` driver

`drivers.mock(script: Resource | [MockStep])`. A script maps prompt hashes (or a
sequence) to responses, tool calls and usage; unmatched prompts fail with a
clear message. Used by `leash test`, CI and golden journals. Supports
`latency_ms` and injected failures (`429`, timeout) for chaos tests.

## 7. Tools

`Tool { name, description, input_schema, output_type, caps_required, impl }`.

Built-ins (`std.tools`):
| Tool | Caps | Notes |
|---|---|---|
| `fs.read(path)`, `fs.glob(pattern)`, `fs.grep(pattern, glob)` | `fs.read` | paths relative to workdir; symlink-resolved |
| `fs.write(path, content)`, `fs.edit(path, old, new)`, `fs.mkdir`, `fs.rm` | `fs.write` | `fs.edit` rejects if the file changed since the agent last read it (staleness check) |
| `shell(cmd, timeout?, cwd?)` | `shell` | no shell interpreter unless allowlisted; output capped (default 1 MiB, CAS) |
| `test(cmd, runner?)` | `shell` | parses Jest/Vitest/pytest/cargo/go/JUnit |
| `git.status/diff/commit/checkout/worktree/log` | `fs.write`+`shell` | restricted to the run worktree; push requires `net` and a gate |
| `http.get/post(url, ...)` | `net` | host allowlist |
| `lint(cmd?)` | `shell` | wraps configured linters |

Custom tools: `tool` declarations (spec §6, tour §9). MCP tools: `mcp.connect`
(stdio or HTTP) with schemas fetched once and cached under `.leash/mcp/<server>.json`
with hashes; `leash check` uses the cache; `leash mcp refresh` updates it.
Runtime uses the official Rust MCP SDK (`rmcp`).

For CLI drivers, custom and MCP tools are exposed through a per-run MCP server
(`leash mcp-serve --run <id>`, stdio) so that the CLI calls back into the
runtime; the runtime applies capabilities, journals the call and returns the
result.

Tool middleware (pre/post hooks): permission check → argument validation
(schema + refinements) → optional program hook → execute → output sanitisation
(secret redaction, size cap) → journal → optional program hook → trace.

## 8. Price table

`std/prices.toml` lists `$/MTok` for input, output, cache read, cache write per
model id. Overridable in `leash.toml`. The runtime warns when a model is not in
the table and counts tokens only.
