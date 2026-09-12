# WP-14: MCP client, Leash `tool` declarations and the per-run MCP server

Milestone: M3 · Size: M · Status: see `docs/status.md`

## Goal
Custom tools become first-class and uniform across drivers. Programs can
declare tools in Leash, import tools from MCP servers with cached schemas, and
attach pre/post hooks. API drivers call these tools through the runtime tool
layer; CLI drivers reach the same tools through a per-run MCP server that
proxies into the running runtime, so capabilities, validation and journaling
apply identically.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/09-drivers-and-tools.md` §7 (tools, MCP, middleware), §3 (how CLI drivers receive `--mcp-config`)
- `docs/02-language-spec.md` §5.5 (effects of tool calls), §6.1, §11 (schema mapping), §12 (`std.mcp`)
- `docs/03-runtime-architecture.md` §8.1
- `docs/07-budgets-and-capabilities.md` §2.3, §6 (schema pinning)
- `docs/05-harness-model.md` §2 (tool lifecycle hooks)
- WP-11 (tool layer and sandbox), WP-12 (capabilities)

## Scope
- MCP client on the official Rust SDK (`rmcp`): stdio transport (child
  process, scrubbed env) and streamable HTTP transport. `mcp.connect(stdio(cmd) | http(url))`
  is a pure descriptor; connection happens on first tool use and is journaled.
- Schema cache: `.leash/mcp/<server-name>.json` = `{ server, fetched_at, tools:
  [{ name, description, input_schema, output_schema?, hash }] }`. `leash check`
  uses the cache and errors with "run `leash mcp refresh <server>`" when
  missing or when the server's live schema hash differs at run start (config
  `[mcp] on_schema_change = "fail" | "warn"`).
- `leash mcp refresh [server]`.
- JSON Schema → Leash type (best effort): object → record (non-required
  properties become `T?`), array → `[T]`, string enum → anonymous enum,
  integer/number/boolean/string → primitives, anything else → `Json`. Results
  are `Json` unless an `outputSchema` is present.
- Leash `tool` declarations (grammar `ToolDecl`): description from the doc
  comment, `input_schema` from parameters (WP-08 schema generation), result
  type, `caps_required` derived from the inferred effect set (`shell` →
  `shell`, `fs.read` → `fs.read`, …). Registering a tool whose `caps_required`
  exceed the run's `allow` fails at run start with `Permission` naming the tool.
- Execution of a Leash tool: validate input against the schema and
  refinements (invalid → `is_error` result to the model, `Tool` failure
  recorded, not a program failure), evaluate the body in a child activation
  with the run's `CapScope`/`BudgetScope`, serialise the result.
- Middleware chain (fixed order): permission check → input validation →
  program `before` hooks → execute → output sanitisation (secret redaction,
  size cap with CAS spill) → program `after` hooks → journal child entry →
  tool trace. `tool.before(fn)` / `tool.after(fn)` in `std.tools` return
  wrapped `Tool` values; hook closures may `fail Permission` to deny.
- Tool service: the runtime listens on a Unix domain socket
  `.leash/runs/<id>/tools.sock` speaking newline-delimited JSON-RPC
  (`list_tools`, `call_tool`). `leash mcp-serve --run <id>` is a thin process
  that speaks MCP over stdio (rmcp server side) and forwards to the socket.
  CLI drivers reference it in their MCP config.
- Every tool call from either path is a child `tool` journal entry of the
  enclosing `run` with `parent`, name, input, output (CAS if large), duration,
  status, and the capability decision.

## Out of scope
- MCP resources and prompts (tools only).
- MCP sampling requests from servers back to the model.
- Tool search / deferred loading on the API side.
- Container-level isolation of MCP server processes (they run under the
  process sandbox with scrubbed env only).

## Interfaces
```rust
pub struct ToolSpec { pub name: String, pub description: String, pub input_schema: serde_json::Value,
                      pub output_type: Ty, pub caps_required: CapSet, pub deterministic: bool }
pub enum ToolImpl { Builtin(Arc<dyn BuiltinTool>), Leash { closure: LirClosure }, Mcp { server: McpRef, name: String } }
pub struct Tool { pub spec: ToolSpec, pub imp: ToolImpl, pub before: Vec<LirClosure>, pub after: Vec<LirClosure> }

pub trait ToolMiddleware: Send + Sync {
    async fn before(&self, call: &ToolCall, ctx: &EffectCtx) -> Result<(), Failure>;
    async fn after(&self, call: &ToolCall, out: &mut ToolOutput, ctx: &EffectCtx) -> Result<(), Failure>;
}
pub struct ToolRegistry { /* name → Tool, ordered */ }
impl ToolExecutor for ToolRegistry { /* as consumed by WP-13 */ }

// tool service wire (socket): {"jsonrpc":"2.0","method":"call_tool","params":{"name":…,"input":…},"id":…}
```
CLI: `leash mcp refresh [server]`, `leash mcp-serve --run <id>` (hidden from
the top-level help; internal).

## Deliverables
- `crates/leash-drivers/src/mcp/{client.rs, cache.rs, schema_to_ty.rs, proxy_server.rs}`
- `crates/leash-effects/src/tools/{registry.rs, middleware.rs, leash_tool.rs, service.rs}`
- `crates/leash-cli/src/cmd/mcp.rs`
- Test MCP server: `crates/leash-drivers/tests/support/mcp_echo_server.rs` (tools `echo`, `add`, one with a nested object schema)
- Tests: schema cache round trip; schema→type mapping table; Leash tool with
  refinement rejects bad input as `is_error`; hook denial; proxy end-to-end
  (spawn `leash mcp-serve` against a running test runtime, call `echo` via an
  rmcp client, assert a child journal entry exists).

## Acceptance criteria
- `cargo nextest run -p leash-drivers -p leash-effects` passes.
- `leash mcp refresh` against the test server writes
  `.leash/mcp/echo.json` with hashes; editing the server's schema and running
  `leash run` with `on_schema_change = "fail"` exits 2 with the tool name.
- `leash check examples/05_mcp_tools.leash` succeeds offline once the cache exists
  (the GitHub server is replaced by the echo server in a test copy of the example).
- A `run` with the mock driver scripted to call `file_issue` from
  `examples/05` produces a journal where the tool call is a child entry whose
  `caps` decision records `human: true` and whose gate answer comes from
  `--gate approve`.
- `leash mcp-serve --run <id>` + an rmcp stdio client lists exactly the tools
  the run's `allow` permits.

## Notes and pitfalls
- Pin `rmcp` to an exact version; the MCP spec evolves. Wrap it in one module.
- Tool names must be unique across builtin, Leash and MCP tools in one run;
  prefix MCP tools as `<server>__<tool>` when exposed to models and map back.
- Anthropic strict tools require `additionalProperties: false` and full
  `required`; MCP schemas often violate this. Normalise a copy for the API
  (adding `additionalProperties: false`, filling `required` with all keys and
  making the rest nullable) and validate results against the original.
- The socket path can exceed the Unix limit (108 bytes) in deep directories;
  fall back to `$XDG_RUNTIME_DIR/leash/<id>.sock` and record the path in the manifest.
- Journal child entries for tools called via the proxy must carry the same
  `parent` as API-path calls; the parent is the current `run`, known to the
  service by run id.

## Dependencies
Hard: WP-11, WP-12. Soft: WP-08 (schema generation for Leash tools; until then hand-write schemas in tests).

## Estimated size
M (2–4 days).
