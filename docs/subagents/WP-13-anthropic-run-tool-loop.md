# WP-13: Anthropic driver (`ask` and `run` tool loop)

Milestone: M5 · Size: L · Status: see `docs/status.md`

## Goal
After this WP, `ask` is complete and `run agent(...)` works against the
Anthropic Messages API over the Boost.Beast client that WP-05 delivers, with
the runtime executing every tool call under the active capability set and
budget. The loop is append-only, streams every turn, uses strict tool schemas,
passes thinking blocks back unchanged, places a prompt-caching breakpoint on
the stable system prefix, sends a task budget when the active scope allows one,
returns a typed final answer, and emits one child journal entry per turn and
per tool call. This is the reference implementation of ADR-0004: for API
drivers the runtime, not the model backend, owns the loop.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which module owns which shared type)
- `docs/09-drivers-and-tools.md` §1 (contract), §2 (request construction, `run` loop), §7 (tools), §8 (prices)
- `docs/02-language-spec.md` §6.2–6.5 (`ask`/`run` semantics), §11 (schema mapping)
- `docs/03-runtime-architecture.md` §7–§9
- `docs/04-determinism-and-replay.md` §3 (journal entry, CAS, child entries)
- `docs/07-budgets-and-capabilities.md` §1.2 (accounting), §2.3 (enforcement point 1)
- `docs/adr/0004-drivers.md`
- WP-05 brief (the Boost.Beast client, wire builder and usage parsing this
  extends), WP-11 (tool layer), WP-12 (budget scopes)

## Scope
- Extend `leash::drivers::anthropic` with the driver surface: configuration,
  credential lookup, the price table, `ask`, and `AnthropicDriver::run` over
  the client that WP-05 delivers.
- Request body per turn: `model`, `max_tokens` (default 64000), `stream: true`,
  `system` blocks (program system prompt, then profile protocol sections; last
  block carries `cache_control: {"type": "ephemeral"}`), `messages`, `tools`
  (`{name, description, input_schema, strict: true}`, stable order), `thinking:
  {"type": "adaptive"}`, `output_config: {effort, format?, task_budget?}`.
- First user message = task text followed by the rendered `Context` (from
  WP-17; until then a plain string) so identical contexts hit the cache.
- SSE streaming: assemble content blocks (`text`, `tool_use`, `thinking`,
  `redacted_thinking`) from `message_start`/`content_block_*`/`message_delta`
  events delivered by the WP-05 client; capture per-turn `usage`.
- Stop-reason handling: `tool_use` → execute all `tool_use` blocks
  concurrently through the tool layer, append the assistant content verbatim,
  then one user message containing every `tool_result` (`is_error: true` for
  failures); `end_turn` → finish; `max_tokens` → append `"Continue."` at most
  twice then `Tool` failure; `refusal` → `Model` failure carrying
  `stop_details`; `pause_turn` → resend unchanged.
- Fallbacks: when the model is `claude-fable-5-1` and config `fallbacks` is
  not `"off"`, send `fallbacks: "default"` with beta header
  `server-side-fallback-2026-07-01`; record `content[].type == "fallback"`
  switch points in the journal `note`.
- Task budget: when the active scope has a token limit and the remaining
  amount is ≥ 20 000, send `output_config.task_budget = {"type": "tokens",
  "total": remaining}` with beta `task-budgets-2026-03-13`; otherwise omit.
- Compaction (optional, config `compaction = true`): beta `compact-2026-01-12`;
  append `response.content` verbatim so compaction blocks survive.
- `max_turns` (request option or `Budget.turns`) ends the loop with a `Budget`
  failure after the last permitted turn.
- Structured final answer: when a schema is present send
  `output_config.format = {"type": "json_schema", "schema": …}`; parse the final
  text; validate with WP-08 validators; on failure re-ask inside the same
  conversation with the error text, at most `retries` times (default 1); then
  `Model` failure.
- Budget accounting: after every turn charge `input_tokens + output_tokens +
  cache_read_input_tokens + cache_creation_input_tokens` and the computed cost
  to the active `BudgetStack` scope; before every turn and every tool call ask
  the scope whether the effect may proceed; cancel the in-flight stream when
  the wall deadline passes.
- Journal: the `run` is one parent `llm.run` entry; each turn is a child
  `llm.turn` entry (request hash, content blocks in CAS, usage, stop reason);
  each tool call is a child `tool` entry. On replay the parent's recorded
  output is returned directly; children are not re-executed.
- `DriverEvent` stream to the `EventSink`: `Turn`, `ToolCall`, `ToolResult`,
  `Text`, `Thinking`, `Usage`, `Error`.
- Driver-level retries for 429/5xx/connection errors: 3 attempts, exponential
  backoff, each attempt noted; `retry-after` honoured.

## Out of scope
- Server-side tools (`web_search`, `code_execution`), the memory tool, MCP
  connector on the API side. Leash tools cover these needs in v1.
- Sessions or conversation continuation across `run` calls.
- Batches API.
- Claude Code / Codex drivers (WP-18, WP-19).

## Interfaces
Provided (in `leash_drivers`):
```cpp
// src/drivers/include/leash/drivers/anthropic/driver.hpp
class AnthropicDriver final : public Driver { /* client (WP-05), config, price table */ };

// one payload type per alternative: std::string alternatives would be indistinguishable
struct Turn { std::uint32_t index = 0; };
struct ToolCall { std::string id; std::string name; nlohmann::json input; };
struct ToolResult { std::string id; nlohmann::json output; bool is_error = false; std::uint64_t duration_ms = 0; };
struct Text { std::string text; };
struct Thinking { std::string text; };
struct Error { std::string message; };

using DriverEvent = std::variant<Turn, ToolCall, ToolResult, Text, Thinking, Usage, Error>;
struct DriverEventVisitor {                      // declared beside the variant; tests reuse it
    void operator()(const Turn&) const;
    void operator()(const ToolCall&) const;
    void operator()(const ToolResult&) const;
    void operator()(const Text&) const;
    void operator()(const Thinking&) const;
    void operator()(const Usage&) const;
    void operator()(const Error&) const;
};

class EventSink {
public:
    virtual ~EventSink() = default;
    virtual void emit(const DriverEvent& ev) = 0;   // may be called from any io_context strand
};
```
Consumed:
```cpp
// WP-11 / WP-14 tool layer
class ToolExecutor {
public:
    virtual ~ToolExecutor() = default;
    virtual std::vector<ToolSpec> specs() const = 0;        // name, description, input_schema (strict-ready)
    virtual asio::awaitable<std::expected<ToolOutput, Failure>> call(std::string_view name,
                                                                    nlohmann::json input,
                                                                    EffectCtx& ctx) = 0;
};

// WP-12: narrow view of the active BudgetStack scope
class BudgetScope {
public:
    virtual ~BudgetScope() = default;
    virtual std::expected<void, Failure> may_perform(EffectKind kind) = 0;
    virtual void charge(const Usage& usage, Money cost) = 0;
    virtual std::optional<std::chrono::steady_clock::time_point> deadline() const = 0;
};
```
Request/response JSON shapes live in one module `anthropic::wire` with plain
structs and free `to_json` / `from_json` functions and no logic, so API drift is
a one-file change.

## Deliverables
- `src/drivers/anthropic/{wire,sse,run_loop,cache}.cpp` with headers under `src/drivers/include/leash/drivers/anthropic/`
- Fixtures: `tests/drivers/fixtures/anthropic/run_3turn/*.sse` (recorded streams for a
  three-turn loop: two parallel `tool_use` blocks, one `tool_use`, then `end_turn` with JSON)
- Tests: `tests/drivers/run_loop_fixture_test.cpp` (fixtures served by the testkit `StubHttpServer`), `tests/drivers/run_loop_live_test.cpp` (CTest label `network`, guarded by `LEASH_TEST_NETWORK`)
- Docs: update `docs/09-drivers-and-tools.md` §2 with any wire detail learned during implementation

## Acceptance criteria
- `ctest --preset dev -R leash_drivers` passes; the fixture test asserts: tool
  calls executed concurrently (both start before either finishes), a single
  user message with two `tool_result` blocks, thinking blocks echoed back
  unchanged in turn 2's request, usage charged per turn, final JSON parsed.
- `ANTHROPIC_API_KEY=… LEASH_TEST_NETWORK=1 ctest --preset dev -L network -R run_loop_live`
  runs `examples/01_hello.leash`-style `run` twice with identical prefix and
  asserts `cache_read_input_tokens > 0` on the second run.
- Journal of a run shows one `llm.run` entry with `children == turns + tool_calls`
  and `leash replay` of that run makes zero HTTP requests (assert with a
  `StubHttpServer` that expects no calls).
- A run with `budget { turns: 2 }` against a fixture that wants three turns
  ends with `Failure { kind: Budget }` and the journal marks the run `failed`.
- `stop_reason: "refusal"` fixture yields `Failure { kind: Model, data.stop_details … }`.

## Notes and pitfalls
- Never edit earlier turns. Preserved-thinking checks on current models reject
  edited history; the loop is append-only by construction.
- Parallel tool results must go back in one user message; splitting them
  teaches the model to stop parallelising.
- Do not set `tool_choice` to `any`/`tool` (rejected on Fable 5.1). Leave `auto`.
- `budget_tokens` is rejected on current models; only `{"type": "adaptive"}`.
- Keep the tools array and system blocks byte-stable within a run; any
  change invalidates the cache. Put volatile text in the user message.
- The price table may lack a model; count tokens and warn, do not fail.
- Cancellation: emit on the request's `asio::cancellation_signal` slot so the
  Beast response stream closes, mark the child entry `cancelled`, and return
  `Budget`/`Tool` as appropriate; the parent entry must still be written.

## Dependencies
Hard: WP-05, WP-11, WP-12. Soft: WP-17 (Context rendering), WP-08 (validators; until then validate with valijson directly).

## Estimated size
L (1–2 weeks).
