# WP-05: Mock driver and Anthropic `ask` driver

Milestone: M1 · Size: M · Status: see `docs/status.md`

## Goal
The `Driver` trait and two implementations: a scriptable `mock` driver for
tests and CI, and an `anthropic` driver that performs `ask` against the
Messages API over raw HTTPS with structured outputs, adaptive thinking, effort,
usage accounting and refusal handling. `run` (agentic sessions with tools) is
WP-13; this WP leaves it returning `DriverError::Unsupported`.

## Read first
- `docs/subagents/shared-interfaces.md` (normative: which crate owns which shared type)
- `docs/09-drivers-and-tools.md` §1 (contract), §2 (anthropic), §6 (mock), §8 (prices)
- `docs/02-language-spec.md` §6.2 (`ask` semantics), §11 (schemas the driver receives)
- `docs/04-determinism-and-replay.md` §3 (what the journal needs from a driver: usage, model, notes)
- `docs/07-budgets-and-capabilities.md` §1.2 (usage fields → cost)
- `docs/adr/0004-drivers.md`, `docs/adr/0001` (raw HTTP is the sanctioned route in Rust)
- The Claude API request shapes recorded in this repository's docs are current as of 2026-06; re-check `anthropic-version`, betas and `output_config` in the official docs before coding.

## Scope
- `crates/leash-drivers` (no dependency on `leash-runtime`):
  - `lib.rs`: `Driver` trait, request/response types, `DriverCtx`, `DriverError`, `DriverRegistry` helpers, `Effort`, `StopReason`, `Usage` (re-export `leash_spec::Usage`).
  - `mock.rs`: `MockDriver::from_script(path)`. Script format (JSON):
    ```json
    { "version": 1,
      "entries": [
        { "match": { "seq": 0 }, "response": { "json": [ {"name": "Leash", "rationale": "…"} ] }, "usage": { "input_tokens": 120, "output_tokens": 40 } },
        { "match": { "prompt_contains": "Suggest three" }, "response": { "text": "not json" } },
        { "match": { "prompt_hash": "b3:…" }, "fail": { "kind": "http", "status": 429, "times": 1 }, "latency_ms": 50 },
        { "match": { "any": true }, "fail": { "kind": "refusal", "category": "cyber" } }
      ] }
    ```
    Matching order: first entry whose `match` holds; `seq` counts calls on
    this driver instance; `prompt_hash` is `b3:` over the exact prompt string;
    `fail.times` limits how often an entry fails before falling through.
    Unmatched prompts return `DriverError::MockUnmatched { prompt_excerpt }`.
    `latency_ms` sleeps (tokio) so cancellation and timeouts can be tested.
  - `anthropic/`: `mod.rs` (`AnthropicDriver::new(config)`), `config.rs`
    (`model` default `claude-opus-5`, `max_tokens_ask` 16000, `effort` default
    `High`, `thinking_display`, `fallbacks: Option<FallbacksMode>` default
    `Default` when the model is `claude-fable-5-1`, `base_url`, `timeout`,
    `max_retries` 3), `request.rs` (builds the JSON body exactly per docs/09
    §2: `model`, `max_tokens`, `stream: true`, `system` as blocks with one
    `cache_control` breakpoint on the last block, `messages`, `thinking:
    {"type":"adaptive"}`, `output_config: {"effort": …, "format": {"type":"json_schema","schema": …}}`
    when a schema is present, `fallbacks` when configured), `sse.rs`
    (parses `message_start`, `content_block_start/delta/stop`,
    `message_delta` (usage, `stop_reason`, `stop_details`), `message_stop`,
    `ping`, `error`), `auth.rs` (`ANTHROPIC_API_KEY` → `x-api-key`; else
    `ANTHROPIC_AUTH_TOKEN` → `Authorization: Bearer` plus header
    `anthropic-beta: oauth-2025-04-20`; else `DriverError::NoCredentials`
    naming both variables), `retry.rs` (429/408/409/5xx/connection errors:
    exponential backoff 1 s·2ⁿ with jitter, honour `retry-after`, max 3; each
    attempt appended to `notes`).
  - Refusal: `stop_reason == "refusal"` → `AskResponse.stop_reason = Refusal { category, explanation }`;
    the runtime turns it into a `Model` failure. Fallback blocks in `content`
    are summarised in `notes` (`"fallback: <from> → <to>"`).
  - Structured output: when `schema` is present the driver returns `json`
    parsed from the text content; the runtime validates (WP-04). The driver
    never validates schemas itself beyond JSON parsing.
  - Large payloads: raw response bodies above 8 KiB are handed to
    `DriverCtx.payload_sink.put()` and referenced by `AskResponse.raw`.
  - Cost: `AskResponse.usage` is filled from `message_start.usage` and
    `message_delta.usage`; the driver does not compute money (runtime uses
    `leash_spec::prices`).
- `leash doctor` (WP-01 stub) gains a row "anthropic credentials: api-key | oauth-token | none".

## Out of scope
`run`/tools/tool loop (WP-13), Claude Code and Codex drivers (WP-18/19),
Managed Agents, compaction, task budgets (WP-13), prompt-caching strategy
beyond the single system breakpoint, batch API.

## Interfaces
```rust
#[async_trait::async_trait]
pub trait Driver: Send + Sync {
    fn id(&self) -> DriverId;                      // DriverId { name: String, model: String, config_hash: String, version: String }
    fn caps(&self) -> DriverCaps;                  // { structured_output: bool, tools: bool, effort: bool, sessions: bool, cost_reporting: bool, cancellation: bool }
    async fn ask(&self, req: AskRequest, ctx: &DriverCtx) -> Result<AskResponse, DriverError>;
    async fn run(&self, req: RunRequest, ctx: &DriverCtx, sink: &dyn EventSink) -> Result<RunResponse, DriverError>;  // Unsupported here
}
pub struct AskRequest { pub system: Vec<PromptBlock>, pub prompt: String, pub schema: Option<serde_json::Value>, pub effort: Option<Effort>,
                        pub model: Option<String>, pub max_tokens: Option<u32>, pub context: Option<String>, pub attachments: Vec<Blob> }
pub struct AskResponse { pub text: String, pub json: Option<serde_json::Value>, pub usage: Usage, pub stop_reason: StopReason, pub raw: PayloadRef, pub notes: Vec<String>, pub model: String }
pub enum StopReason { EndTurn, MaxTokens, ToolUse, PauseTurn, Refusal { category: Option<String>, explanation: Option<String> }, Other(String) }
pub struct DriverCtx { pub run_id: String, pub cancel: tokio_util::sync::CancellationToken, pub payload_sink: std::sync::Arc<dyn PayloadSink> }
pub trait PayloadSink: Send + Sync { fn put(&self, bytes: &[u8]) -> PayloadRef; }
// PayloadRef is leash-spec's; the runtime supplies the PayloadSink implementation.
#[derive(thiserror::Error, Debug)] pub enum DriverError { NoCredentials { looked_at: Vec<&'static str> }, Http { status: u16, body: String, retried: u8 },
    Transport(String), Protocol(String), Unsupported(&'static str), MockUnmatched { prompt_excerpt: String }, Cancelled, Timeout }
pub enum Effort { Low, Medium, High, XHigh, Max }
```
`DriverId.config_hash` covers every config field that changes responses
(model, effort, max_tokens, thinking, fallbacks, base_url), so the runtime's
`input_hash` (WP-04) can include it.

## Deliverables
- `crates/leash-drivers` with `mock` and `anthropic` (ask only).
- `tests/fixtures/anthropic/*.sse` recorded streams: `ask_text.sse`,
  `ask_json.sse`, `refusal.sse`, `rate_limited_then_ok.sse` (two responses),
  `error_event.sse`; `tests/fixtures/mock/*.json`.
- Unit tests using `wiremock` for HTTP and fixture files for SSE.
- One `#[ignore]` live test `anthropic_live_smoke`.

## Acceptance criteria
- `cargo nextest run -p leash-drivers` passes: request builder snapshot
  (`insta`) for an `ask` with schema shows `thinking.adaptive`,
  `output_config.effort`, `output_config.format.json_schema`, `stream: true`,
  single `cache_control` on the last system block; SSE fixtures yield expected
  `text`/`json`/`usage`/`stop_reason`; `rate_limited_then_ok` results in one
  retry and one note; `refusal.sse` yields `StopReason::Refusal { category: Some("cyber"), .. }`;
  a `wiremock` server returning 500 three times then 200 succeeds with
  `retried == 3`; a fourth failure returns `DriverError::Http`.
- Mock driver tests: `seq`, `prompt_contains`, `prompt_hash`, `any` matching;
  `fail.times` exhaustion; `latency_ms` respects cancellation (`cancel.cancel()`
  returns `DriverError::Cancelled` within 100 ms).
- `env -u ANTHROPIC_API_KEY -u ANTHROPIC_AUTH_TOKEN cargo run -p leash-cli -- doctor` shows `anthropic credentials: none`.
- `ANTHROPIC_API_KEY=… cargo nextest run -p leash-drivers --run-ignored all -E 'test(anthropic_live_smoke)'`
  passes: asks `claude-opus-5` for `{"answer": Int}` with schema and receives `json` matching it.
- With WP-04 merged: `leash run examples/01_hello.leash --input '{"topic":"harness"}'` against the real API completes with a typed list (M1 exit criterion; document the run id in the PR).

## Notes and pitfalls
- Never send `temperature`, `top_p`, `budget_tokens` or assistant prefill; the
  current models reject them (docs/09 §2 and the API reference). Record
  requested-but-unsupported options in `notes`.
- `max_tokens` for `ask` defaults to 16000; always stream (SSE) so long
  answers do not hit HTTP timeouts.
- Use `reqwest` with `rustls-tls`; set a 10 min request timeout; propagate
  `CancellationToken` by aborting the stream.
- Keep every API constant (URL, version header, beta header names, field
  names) in `anthropic/consts.rs` with a comment giving the date verified.
- Do not log prompts or responses through `tracing` at info level; debug only, truncated.
- The driver must be `Clone`-able cheaply (`Arc` inside) so the registry can share it across branches later.

## Dependencies
Hard: WP-02 (Usage, prices). Soft: WP-04 (integration criterion).

## Estimated size
M (3–4 days).
